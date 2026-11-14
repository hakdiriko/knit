// Render the one-page manual (dendrite-manual.html) to dendrite-manual.pdf with headless Chromium.
//
//     node docs/manual/render_manual.js [preview.png]
//
// Needs Playwright (npm i playwright) and its Chromium.
const path = require("path");
const { pathToFileURL } = require("url");
const { chromium } = require("playwright");

(async () => {
  const here = __dirname;
  const browser = await chromium.launch();
  const page = await browser.newPage({ viewport: { width: 794, height: 1123 }, deviceScaleFactor: 2 });
  await page.goto(pathToFileURL(path.join(here, "dendrite-manual.html")).href);
  await page.evaluate(() => document.fonts.ready);
  const overflow = await page.evaluate(() => {
    const main = document.querySelector("main");
    const footer = document.querySelector("footer");
    return document.body.scrollHeight > document.body.clientHeight + 1 || main.scrollHeight > main.clientHeight + 1
      || footer.getBoundingClientRect().bottom > document.body.getBoundingClientRect().bottom + 1;
  });
  if (overflow) {
    console.error("the manual no longer fits on one page");
    process.exit(1);
  }
  if (process.argv[2]) await page.screenshot({ path: process.argv[2] });
  await page.pdf({ path: path.join(here, "dendrite-manual.pdf"), format: "A4", printBackground: true,
                   margin: { top: "0", right: "0", bottom: "0", left: "0" } });
  await browser.close();
})();
