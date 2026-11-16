"""Browser smoke test for the firmware UI with explicit mocked HTTP API."""
from functools import partial
from http.server import SimpleHTTPRequestHandler, ThreadingHTTPServer
from pathlib import Path
from threading import Thread
import json
import os
from playwright.sync_api import sync_playwright

ROOT = Path(__file__).resolve().parents[1]
OUT = ROOT/'build/ui'
OUT.mkdir(parents=True, exist_ok=True)
CONFIG = dict(ssid='Test WLAN', broker='192.168.1.10', mqttUser='wtt', base='wtt2mqtt/test', hostname='wtt2mqtt',
              port=1883, publishSeconds=10, rx=16, tx=17, minutes=30, discovery=True, json=True)
STATE = dict(firmware='0.1.0', uptime=200, online=True, phase='no_flame_observed', requested=False, cooldown=False,
             temperature=48, voltage=12.64, flame=False, power=0, sensor_age_seconds=1, glow_milliohm=1388,
             remaining_seconds=0, communication_error='ok', successes=93, failures=2, command_result='stop acknowledged; cooldown estimated 180 s',
             device='', wbus_version='', manufacture='', counters=None, fault_count=1, fault_age_seconds=2,
             faults=[dict(code='0x02',count=2,text='Kein Start')], update_safe=True, ota_active=False, free_heap=210000,
             wifi_connected=True, mqtt_connected=True, ap=False, ip='192.168.1.60', rssi=-52)
requests = []
class Handler(SimpleHTTPRequestHandler):
    def log_message(self, *_):
        pass
    def answer(self, value, status=200):
        body = json.dumps(value).encode()
        self.send_response(status)
        self.send_header('Content-Type','application/json')
        self.send_header('Content-Length',str(len(body)))
        self.end_headers()
        self.wfile.write(body)
    def do_GET(self):
        if self.path=='/api/status': self.answer(STATE)
        elif self.path=='/api/config': self.answer(CONFIG)
        elif self.path=='/api/log':
            body=b'[100] W-Bus TX F4 03 50 05 A2\n[150] W-Bus RX 4F 0B D0 ...\n'
            self.send_response(200); self.send_header('Content-Type','text/plain'); self.end_headers(); self.wfile.write(body)
        elif self.path=='/api/scan': self.answer(dict(networks=[dict(ssid='Test WLAN',rssi=-52)]))
        else: super().do_GET()
    def do_POST(self):
        data=self.rfile.read(int(self.headers.get('Content-Length',0)))
        assert self.headers.get('X-WTT2MQTT')=='1'
        requests.append((self.path,json.loads(data)))
        if self.path=='/api/config': CONFIG.update(json.loads(data))
        self.answer(dict(queued=True,saved=True,restart_required=True))

server=ThreadingHTTPServer(('127.0.0.1',0),partial(Handler,directory=str(ROOT/'web')))
Thread(target=server.serve_forever,daemon=True).start()
try:
    with sync_playwright() as p:
        # Use an installed Edge or a Playwright Chromium, without changing the user's browser session.
        edge=Path('C:/Program Files (x86)/Microsoft/Edge/Application/msedge.exe')
        options=dict(headless=True)
        if edge.exists(): options['executable_path']=str(edge)
        browser=p.chromium.launch(**options)
        for width,height in [(1280,1000),(390,844)]:
            page=browser.new_page(viewport=dict(width=width,height=height))
            errors=[]
            page.on('pageerror',lambda err:errors.append(str(err)))
            page.goto(f'http://127.0.0.1:{server.server_port}/')
            page.wait_for_function("document.getElementById('temperature').textContent === '48'")
            assert page.locator('#start').is_enabled()
            page.screenshot(path=str(OUT/f'dashboard-{width}.png'),full_page=True)
            for route in ['diagnose','mqtt','wifi','firmware','debug','dashboard']:
                page.locator(f'nav a[href="#{route}"]').click()
                page.locator(f'#{route}').wait_for(state='visible')
                assert page.evaluate('document.documentElement.scrollWidth <= innerWidth'), route
            page.locator('#start').click()
            page.wait_for_function("!document.getElementById('notice').hidden")
            assert any(path=='/api/command' and payload['action']=='start' for path,payload in requests)
            page.locator('nav a[href="#mqtt"]').click()
            page.locator('#mqttForm input[name="port"]').fill('1884')
            page.locator('#mqttForm button').click()
            page.wait_for_timeout(100)
            assert CONFIG['port']==1884
            assert 'mqttPassword' not in [data for path,data in requests if path=='/api/config'][-1]
            page.locator('nav a[href="#wifi"]').click()
            page.locator('#scan').click()
            page.wait_for_function("document.querySelectorAll('#networks option').length > 0")
            page.locator('nav a[href="#diagnose"]').click()
            assert 'Kein Start' in page.locator('#faults').inner_text()
            assert 'Nicht verfügbar' in page.locator('#deviceInfo').inner_text()
            page.locator('#diagnoseButton').click()
            page.wait_for_timeout(100)
            assert any(path=='/api/command' and data['action']=='diagnose' for path,data in requests)
            STATE.update(online=False,temperature=None,voltage=None,flame=None,power=None,update_safe=False,phase='unknown')
            page.wait_for_function("document.getElementById('start').disabled")
            assert page.locator('#uploadButton').is_disabled()
            page.screenshot(path=str(OUT/f'offline-{width}.png'),full_page=True)
            STATE.update(online=True,temperature=48,voltage=12.64,flame=False,power=0,update_safe=True,phase='no_flame_observed')
            assert not errors, errors
            page.close()
            print(f'PASS browser {width}px: six routes, no overflow, commands, config, scan, faults, stale-state controls; no JS errors')
        browser.close()
finally:
    server.shutdown()
