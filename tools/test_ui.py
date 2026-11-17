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
              port=1883, publishSeconds=10, rx=16, tx=17, minutes=30, discovery=True, json=True,
              adminUser='', adminPasswordSet=False)
STATE = dict(firmware='0.1.0', uptime=200, online=True, phase='no_flame_observed', requested=False, cooldown=False,
             temperature=48, voltage=12.64, flame=False, power=0, sensor_age_seconds=1, glow_milliohm=1388,
             remaining_seconds=0, communication_error='ok', successes=93, failures=2, command_result='stop acknowledged; cooldown estimated 180 s',
             device='', wbus_version='', manufacture='', counters=None, fault_count=1, fault_age_seconds=2,
             faults=[dict(code='0x02',count=2,text='Kein Start')], update_safe=True, ota_active=False, free_heap=210000,
             wifi_connected=True, mqtt_connected=True, ap=False, ip='192.168.1.60', rssi=-52)
STATE.update(scan_active=False,scan_profile=0,scan_results=[dict(name='TT-C KWP 10400/8N1 25/25',result='untested')])
requests = []
LOG_TEXT = ''.join(f'[{index}] W-Bus test line {index}\n' for index in range(80))
def navigate(page, route):
    page.evaluate("location.hash = 'menu'")
    page.locator('#menu').wait_for(state='visible')
    page.locator(f'#menu a[href="#{route}"]').click()
    page.locator(f'#{route}').wait_for(state='visible')

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
        elif self.path=='/api/wifi': self.answer(dict(ssid=CONFIG['ssid'],hostname=CONFIG['hostname'],wifiPasswordSet=True,connected=False,retry_seconds=30))
        elif self.path=='/api/log':
            body=LOG_TEXT.encode()
            self.send_response(200); self.send_header('Content-Type','text/plain'); self.end_headers(); self.wfile.write(body)
        elif self.path=='/api/scan-log':
            body=b'SCAN_WAKE_LOW us=25000 RX=L->H edges=1 low_max_us=12000\nSCAN attempt service=A1 result=positive_response\n'
            self.send_response(200); self.send_header('Content-Type','text/plain'); self.end_headers(); self.wfile.write(body)
        elif self.path=='/api/scan': self.answer(dict(networks=[dict(ssid='Test WLAN',rssi=-52)]))
        else: super().do_GET()
    def do_POST(self):
        data=self.rfile.read(int(self.headers.get('Content-Length',0)))
        assert self.headers.get('X-WTT2MQTT')=='1'
        if self.path=='/api/update':
            requests.append((self.path,dict(bytes=len(data))))
            self.answer(dict(uploaded=True))
            return
        requests.append((self.path,json.loads(data)))
        if self.path=='/api/config': CONFIG.update(json.loads(data))
        elif self.path=='/api/wifi': CONFIG.update(json.loads(data))
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
            assert page.locator('.shell').bounding_box()['width'] <= 520
            assert page.locator('nav').is_hidden()
            page.screenshot(path=str(OUT/f'dashboard-{width}.png'),full_page=True)
            for route in ['diagnose','mqtt','wifi','firmware','debug','dashboard']:
                navigate(page,route)
                page.locator(f'#{route}').wait_for(state='visible')
                assert page.evaluate('document.documentElement.scrollWidth <= innerWidth'), route
            page.locator('#start').click()
            page.wait_for_function("!document.getElementById('notice').hidden")
            assert any(path=='/api/command' and payload['action']=='start' for path,payload in requests)
            navigate(page,'mqtt')
            page.locator('#mqttForm input[name="port"]').fill('1884')
            page.locator('#mqttForm button').click()
            page.wait_for_timeout(100)
            assert CONFIG['port']==1884
            assert 'mqttPassword' not in [data for path,data in requests if path=='/api/config'][-1]
            navigate(page,'wifi')
            page.locator('#scan').click()
            page.wait_for_function("document.querySelectorAll('#networks option').length > 0")
            # Optional web login: explicitly set credentials, preserve an existing
            # password on a later blank field, then remove both via disable control.
            page.locator('#securityForm input[name="adminUser"]').fill('my-user')
            page.locator('#securityForm input[name="adminPassword"]').fill('my-password')
            page.locator('#securityForm button').click()
            page.wait_for_timeout(100)
            login = [data for path,data in requests if path=='/api/config'][-1]
            assert login == dict(adminUser='my-user', adminPassword='my-password')
            page.locator('#securityForm input[name="adminPassword"]').fill('')
            page.locator('#securityForm button').click()
            page.wait_for_timeout(100)
            login = [data for path,data in requests if path=='/api/config'][-1]
            assert login == dict(adminUser='my-user')
            page.locator('#securityForm input[name="disableWebAuth"]').check()
            page.locator('#securityForm button').click()
            page.wait_for_timeout(100)
            login = [data for path,data in requests if path=='/api/config'][-1]
            assert login == dict(adminUser='', adminPassword='')
            navigate(page,'diagnose')
            assert 'Kein Start' in page.locator('#faults').inner_text()
            assert 'Nicht verfügbar' in page.locator('#deviceInfo').inner_text()
            page.locator('#diagnoseButton').click()
            page.wait_for_timeout(100)
            assert any(path=='/api/command' and data['action']=='diagnose' for path,data in requests)
            page.locator('#protocolScan').click()
            page.wait_for_timeout(100)
            assert any(path=='/api/command' and data['action']=='scan' for path,data in requests)
            STATE.update(scan_active=True,scan_profile=1,scan_results=[dict(name=name,result="untested") for name in ["W-Bus","TT-C KWP 25/25","TT-C KWP 30/25","TT-C WTT","Fiat","PSA","BMW"]])
            page.wait_for_function("document.getElementById('protocolScan').disabled")
            assert page.locator('#diagnoseButton').is_disabled()
            assert page.locator('#protocolScanStop').is_enabled()
            assert '2/7' in page.locator('#protocolScanState').inner_text()
            page.locator('#protocolScanStop').click()
            page.wait_for_timeout(100)
            assert any(path=='/api/command' and data['action']=='scan-stop' for path,data in requests)
            STATE.update(scan_active=False,scan_results=[dict(name='TT-C KWP 10400/8N1 25/25',result='positive_response')])
            page.wait_for_function("document.getElementById('protocolScanResults').textContent.includes('positive')")
            page.locator('#diagnose details summary').click()
            page.locator('#scanLogLoad').click()
            page.wait_for_function("document.getElementById('scanLog').value.includes('SCAN_WAKE_LOW')")
            saved=page.locator('#scanLog').input_value()
            assert 'service=A1' in saved
            with page.expect_download() as scan_download:
                page.locator('#scanLogDownload').click()
            assert Path(scan_download.value.path()).read_text(encoding='utf-8')==saved
            STATE.update(online=False,temperature=None,voltage=None,flame=None,power=None,update_safe=True,phase='unknown')
            page.wait_for_function("document.getElementById('start').disabled")
            assert page.locator('#uploadButton').is_enabled()
            navigate(page,'firmware')
            assert 'auch ohne angeschlossenes Heizger' in page.locator('#updateState').inner_text()
            page.locator('#firmwareFile').set_input_files(dict(name='WTT2MQTT_test.bin.ota',mimeType='application/octet-stream',buffer=b'\xe9'+b'\0'*31))
            with page.expect_response('**/api/update'):
                page.locator('#uploadButton').click()
            assert any(path=='/api/update' and data['bytes']>32 for path,data in requests)
            page.screenshot(path=str(OUT/f'offline-{width}.png'),full_page=True)
            navigate(page,'debug')
            page.wait_for_function("document.getElementById('log').value.includes('test line 79')")
            assert page.locator('#log').get_attribute('readonly') is not None
            assert page.evaluate('logView.atBottom()')
            original=page.locator('#log').input_value()
            page.evaluate('logView.snapshot(logView.previous.join("\\n")+"\\n")')
            assert page.locator('#log').input_value()==original
            page.evaluate("logView.field.scrollTop=0")
            page.wait_for_function('logView.locked')
            LOG_TEXT += f'[{width}] new arrival\n'
            page.wait_for_function('logView.buffer !== logView.field.value')
            assert page.locator('#log').input_value()==original
            assert page.evaluate('logView.field.scrollTop')==0
            page.locator('#logLock').click()
            assert page.evaluate('logView.atBottom() && !logView.locked')
            # Unlock also follows the tail when no new data arrived.
            page.evaluate('logView.field.scrollTop=0')
            page.wait_for_function('logView.locked')
            page.locator('#logLock').click()
            assert page.evaluate('logView.atBottom()')
            page.evaluate('logView.field.focus(); logView.field.setSelectionRange(0,20)')
            page.wait_for_function('logView.locked')
            frozen=page.locator('#log').input_value()
            page.evaluate('logView.append("pending line\\n")')
            assert page.locator('#log').input_value()==frozen
            assert page.evaluate('logView.field.selectionEnd-logView.field.selectionStart')==20
            # Exercise HTTP clipboard fallback without touching the real clipboard.
            page.evaluate("Object.defineProperty(navigator,'clipboard',{value:undefined,configurable:true}); document.execCommand=()=>{window.copied=document.activeElement.value; return true;}")
            page.locator('#copyLog').click()
            assert page.evaluate('window.copied')==frozen
            page.locator('#selectLog').click()
            assert page.evaluate('logView.field.selectionEnd-logView.field.selectionStart')==len(frozen)
            page.locator('#logWrap').uncheck()
            assert page.locator('#log').get_attribute('wrap')=='off'
            with page.expect_download() as download:
                page.locator('#downloadLog').click()
            assert Path(download.value.path()).read_text(encoding='utf-8')==frozen
            page.screenshot(path=str(OUT/f'debug-{width}.png'),full_page=True)
            page.locator('#clearLog').click()
            page.evaluate('logView.snapshot(logView.previous.join("\\n")+"\\n")')
            assert page.locator('#log').input_value()==''
            page.evaluate('serialView.append("USB line\\n".repeat(80))')
            assert page.evaluate('serialView.atBottom()')
            page.locator('#serialLock').click()
            page.evaluate('serialView.append("held\\n")')
            assert 'held' not in page.locator('#serialLog').input_value()
            page.locator('#serialLock').click()
            assert page.locator('#serialLog').input_value().endswith('held\n')
            print(f'PASS log {width}px: follow, scroll lock, selection, HTTP copy, download, clear, USB log')
            STATE.update(online=True,temperature=48,voltage=12.64,flame=False,power=0,update_safe=True,phase='no_flame_observed')
            assert not errors, errors
            page.close()
            print(f'PASS browser {width}px: six routes, no overflow, commands, config, scan, faults, stale-state controls; no JS errors')
            setup=browser.new_page(viewport=dict(width=width,height=height))
            setup.on('pageerror',lambda err:errors.append(str(err)))
            setup.goto(f'http://127.0.0.1:{server.server_port}/setup.html')
            setup.wait_for_function("document.querySelector('#setupForm input[name=ssid]').value !== ''")
            assert setup.locator('nav').count()==0
            assert setup.locator('#start,#stop,#otaForm,#mqttForm,#securityForm,#hardwareForm').count()==0
            assert setup.evaluate('document.documentElement.scrollWidth <= innerWidth')
            setup.locator('input[name="ssid"]').fill('Replacement WLAN')
            setup.locator('input[name="wifiPassword"]').fill('replacement-password')
            with setup.expect_response('**/api/wifi'):
                setup.locator('#save').click()
            payload=[data for path,data in requests if path=='/api/wifi'][-1]
            assert payload==dict(ssid='Replacement WLAN',hostname=CONFIG['hostname'],wifiPassword='replacement-password')
            setup.screenshot(path=str(OUT/f'setup-{width}.png'),full_page=True)
            assert not errors,errors
            setup.close()
            print(f'PASS setup {width}px: WLAN form only, restricted save payload, no JS errors or overflow')
        browser.close()
finally:
    server.shutdown()
