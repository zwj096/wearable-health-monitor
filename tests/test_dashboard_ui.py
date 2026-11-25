"""Optional browser regression: set WHM_PLAYWRIGHT_PACKAGE to playwright-core.

Uses an existing Playwright installation and its Node runtime; never downloads
packages or browsers. WHM_BROWSER may select an existing Chromium executable.
"""

import os
from pathlib import Path
import shutil
import subprocess
from http.server import ThreadingHTTPServer
from threading import Thread
import unittest

from app.dashboard_server import DashboardHandler


BROWSER_CHECKS = r"""
const assert = require('assert');
const {chromium} = require(process.argv[1]);
const base = process.argv[2];
(async () => {
  const browser = await chromium.launch({headless: true,
    ...(process.env.WHM_BROWSER ? {executablePath: process.env.WHM_BROWSER} : {})});
  const page = await browser.newPage({acceptDownloads: true});
  page.setDefaultTimeout(5000);
  const errors = [];
  page.on('pageerror', error => errors.push(error.message));
  let requests = 0;
  page.on('request', request => {if (request.url().includes('/api/run')) requests++;});
  async function check(name, body) {
    try {await body(); console.log('PASS ' + name);}
    catch (error) {errors.push(name + ': ' + error.message); console.log('FAIL ' + name);}
  }
  async function run(mode) {
    await page.selectOption('#mode', mode);
    const response = page.waitForResponse(response => response.url().includes('/api/run'));
    await page.click('#run');
    const data = await (await response).json();
    await page.waitForFunction(() => !document.querySelector('#run').disabled);
    return data;
  }
  try {
    await page.goto(base);
    await page.waitForLoadState('networkidle');
    await check('nominal temperature follows actual output', async () => {
      const data = await run('nominal');
      assert.strictEqual(await page.textContent('#temp'), data.latest.temperature_c.toFixed(2) + ' °C');
      assert(data.latest.heart_rate_valid);
      assert((await page.textContent('#hr')).includes('75.0'));
    });
    await check('no-contact heart rate is unavailable and chart has no false zeros', async () => {
      await run('nocontact');
      assert.strictEqual(await page.textContent('#hr'), '-- BPM');
      assert.strictEqual(await page.locator('#chartDots circle').count(), 0);
      assert(!(await page.textContent('#rows')).includes('0.0 BPM'));
      assert.strictEqual(await page.getAttribute('#orb', 'data-state'), '1');
    });
    await check('motion scenario updates state and timestamp labels', async () => {
      const data = await run('motion');
      assert.strictEqual(await page.textContent('#modeLabel'), '运动干扰');
      assert.strictEqual(await page.textContent('#timestamp'), (data.latest.timestamp_ms / 1000).toFixed(2) + ' s');
      assert.strictEqual(await page.getAttribute('#orb', 'data-state'), String(data.latest.state));
      assert((await page.textContent('#gridLines')).includes((data.rows[0].timestamp_ms / 1000).toFixed(2)));
    });
    await check('CSV exports current result without another simulation request', async () => {
      for (const mode of ['nominal', 'nocontact']) {
        const data = await run(mode);
        const before = requests;
        const [download] = await Promise.all([page.waitForEvent('download'), page.click('#download')]);
        const fs = require('fs');
        const text = fs.readFileSync(await download.path(), 'utf8').replace(/^\uFEFF/, '');
        const lines = text.trim().split(/\r?\n/);
        assert.strictEqual(lines.length, data.rows.length + 1);
        const names = lines[0].split(',');
        const cells = lines[lines.length - 1].split(',').map(cell => cell.replace(/^"|"$/g, ''));
        assert.strictEqual(Number(cells[names.indexOf('temperature_c')]), data.latest.temperature_c);
        assert.strictEqual(cells[names.indexOf('heart_rate_valid')], String(data.latest.heart_rate_valid));
        assert.strictEqual(cells[names.indexOf('heart_rate_bpm')], mode === 'nocontact' ? '' : '75');
        assert.strictEqual(Number(cells[names.indexOf('heart_rate_bpm_raw')]), mode === 'nocontact' ? 0 : 75);
        assert.strictEqual(requests, before);
      }
    });
    await check('invalid points split chart paths and use actual time spacing', async () => {
      const row = {mode:'motion', sequence:0, timestamp_ms:1000, state:3,
        state_label:'有效测量', heart_rate_bpm:75, heart_rate_bpm_raw:75,
        heart_rate_valid:true, temperature_c:33.38, quality:100, motion_mg:0,
        battery_mv:3800, flags:1};
      const rows = [row, {...row, sequence:1, timestamp_ms:2000}, {...row, sequence:2, timestamp_ms:3000, state:4,
        state_label:'低质量', heart_rate_bpm:null, heart_rate_bpm_raw:0,
        heart_rate_valid:false, flags:0}, {...row, sequence:3, timestamp_ms:5000}];
      await page.route('**/api/run?**', route => route.fulfill({contentType:'application/json',
        body:JSON.stringify({mode:'motion', latest:rows[3], rows})}));
      await run('motion');
      assert.strictEqual(await page.locator('#chartLine polyline').count(), 2);
      assert.strictEqual(await page.locator('#chartDots circle').count(), 3);
      const x = await page.locator('#chartDots circle').evaluateAll(dots => dots.map(dot => Number(dot.getAttribute('cx'))));
      assert(Math.abs((x[1] - x[0]) / (x[2] - x[0]) - 0.25) < 0.01);
      await page.unroute('**/api/run?**');
    });
    await check('invalid duration is rejected without starting a simulation', async () => {
      const before = requests;
      await page.fill('#seconds', '7');
      await page.click('#run');
      assert.strictEqual(await page.textContent('#state'), '参数有误');
      assert.strictEqual(await page.textContent('#temp'), '-- °C');
      assert.strictEqual(requests, before);
      await page.fill('#seconds', '12');
    });
    await check('failed run clears stale cards, chart and export', async () => {
      await page.route('**/api/run?**', route => route.fulfill({status:400,
        contentType:'application/json', body:JSON.stringify({error:'test simulator failure'})}));
      await run('nominal');
      assert.strictEqual(await page.textContent('#temp'), '-- °C');
      assert.strictEqual(await page.textContent('#hr'), '-- BPM');
      assert.strictEqual(await page.locator('#chartDots circle').count(), 0);
      assert.strictEqual(await page.locator('#chartLine polyline').count(), 0);
      assert(await page.isDisabled('#download'));
      assert((await page.textContent('#rows')).includes('暂无数据'));
      await page.unroute('**/api/run?**');
    });
    if (process.env.WHM_UI_SCREENSHOT) {
      await page.fill('#seconds', '30');
      await run('nominal');
      await page.screenshot({path:process.env.WHM_UI_SCREENSHOT, fullPage:true});
    }
    await check('phone viewport fits and chart labels remain readable', async () => {
      await page.setViewportSize({width:390, height:844});
      await run('nominal');
      assert(await page.evaluate(() => document.documentElement.scrollWidth <= window.innerWidth));
      assert(await page.locator('#gridLines text').first().evaluate(text =>
        parseFloat(getComputedStyle(text).fontSize) * text.getScreenCTM().a >= 10));
    });
    if (process.env.WHM_UI_MOBILE_SCREENSHOT) await page.screenshot({path:process.env.WHM_UI_MOBILE_SCREENSHOT, fullPage:true});
  } finally {await browser.close();}
  if (errors.length) {console.error(errors.join('\n')); process.exitCode = 1;}
})().catch(error => {console.error(error); process.exitCode = 1;});
"""


class DashboardBrowserTest(unittest.TestCase):
    def test_browser_regressions(self):
        package_text = os.environ.get("WHM_PLAYWRIGHT_PACKAGE")
        if not package_text:
            self.skipTest("Set WHM_PLAYWRIGHT_PACKAGE to an existing playwright-core package for UI checks")
        package = Path(package_text)
        bundled_node = package.parent / ("node.exe" if os.name == "nt" else "node")
        node = os.environ.get("WHM_PLAYWRIGHT_NODE") or (str(bundled_node) if bundled_node.exists() else shutil.which("node"))
        if not node:
            self.skipTest("No Node runtime found; set WHM_PLAYWRIGHT_NODE")
        server = ThreadingHTTPServer(("127.0.0.1", 0), DashboardHandler)
        thread = Thread(target=server.serve_forever, daemon=True)
        thread.start()
        try:
            result = subprocess.run(
                [str(node), "-e", BROWSER_CHECKS, str(package), f"http://127.0.0.1:{server.server_port}"],
                capture_output=True, text=True, encoding="utf-8", errors="replace", timeout=90,
            )
            self.assertEqual(result.returncode, 0, result.stdout + result.stderr)
            print(result.stdout, end="")
        finally:
            server.shutdown()
            server.server_close()
            thread.join()


if __name__ == "__main__":
    unittest.main()
