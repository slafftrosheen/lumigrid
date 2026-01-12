
import asyncio
from playwright.async_api import async_playwright, expect

async def main():
    async with async_playwright() as p:
        browser = await p.chromium.launch(headless=True)
        page = await browser.new_page()

        await page.goto("http://localhost:5173")

        # Take a screenshot
        await page.screenshot(path="verification.png")

        await browser.close()

if __name__ == "__main__":
    asyncio.run(main())
