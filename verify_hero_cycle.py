
import asyncio
from playwright.async_api import async_playwright, expect

async def main():
    async with async_playwright() as p:
        browser = await p.chromium.launch(headless=True)
        page = await browser.new_page()

        await page.goto("http://localhost:5173")

        # Wait for the first image to load
        await expect(page.locator(".hero-showcase img")).to_be_visible()

        # Get the src of the first image
        first_image_src = await page.locator(".hero-showcase img").get_attribute("src")

        # Wait for the image to change (max 7 seconds)
        await page.wait_for_function(
            f"""
            () => {{
                const img = document.querySelector(".hero-showcase img");
                return img && img.src !== "{first_image_src}";
            }}
            """,
            timeout=7000
        )

        # Take a screenshot
        await page.screenshot(path="verification.png")

        await browser.close()

if __name__ == "__main__":
    asyncio.run(main())
