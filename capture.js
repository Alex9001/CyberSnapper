const { chromium } = require('playwright');
const fs = require('fs');

(async () => {
    let urls = [];
    const args = process.argv.slice(2);

    if (args.length === 0) {
        console.error('Usage: node capture.js <url1> <url2> ... OR node capture.js urls.txt');
        process.exit(1);
    }

    if (args.length === 1 && fs.existsSync(args[0])) {
        // Read URLs from a file
        const fileContent = fs.readFileSync(args[0], 'utf-8');
        urls = fileContent.split('\n').map(line => line.trim()).filter(line => line.length > 0);
    } else {
        // Read URLs from command line arguments
        urls = args;
    }

    if (urls.length === 0) {
        console.error('No valid URLs provided.');
        process.exit(1);
    }

    // Define your 3 preset resolutions
    const viewports = [
        { name: 'desktop', width: 1920, height: 1080 },
        { name: 'tablet', width: 768, height: 1024 },
        { name: 'mobile', width: 375, height: 812 }
    ];

    const outDir = 'screenshots';
    if (!fs.existsSync(outDir)) fs.mkdirSync(outDir, { recursive: true });

    console.log('Launching browser...');
    const browser = await chromium.launch({ 
        headless: true, 
        args: ['--hide-scrollbars'] 
    });
    
    // Use a single context to utilize caching if needed
    const context = await browser.newContext();
    const page = await context.newPage();

    for (let i = 0; i < urls.length; i++) {
        let targetUrl = urls[i];
        if (!targetUrl.startsWith('http://') && !targetUrl.startsWith('https://')) {
            targetUrl = 'https://' + targetUrl;
        }

        console.log(`\n======================================================`);
        console.log(`Processing URL ${i + 1}/${urls.length}: ${targetUrl}`);
        console.log(`======================================================`);

        // Create a safe filename prefix from the URL
        let urlObj;
        try {
            urlObj = new URL(targetUrl);
        } catch (e) {
            console.error(`Invalid URL: ${targetUrl}`);
            continue;
        }
        
        const safeHostname = urlObj.hostname.replace(/[^a-z0-9]/gi, '_').toLowerCase();
        let safePath = urlObj.pathname.replace(/[^a-z0-9]/gi, '_').toLowerCase();
        if (safePath === '_' || safePath === '') safePath = '';
        const filePrefix = `${safeHostname}${safePath}`;

        for (const vp of viewports) {
            console.log(`\n[${targetUrl}] Capturing ${vp.name} (${vp.width}x${vp.height})...`);
            
            await page.setViewportSize({ width: vp.width, height: vp.height });
            
            try {
                await page.goto(targetUrl, { waitUntil: 'domcontentloaded', timeout: 60000 });
                
                // Force hide scrollbars via CSS in case the site uses custom scrollbar styling
                await page.addStyleTag({ content: `
                    ::-webkit-scrollbar { display: none !important; }
                    * { scrollbar-width: none !important; }
                `});
            } catch (err) {
                console.error(`Failed to load ${targetUrl}: ${err.message}`);
                continue;
            }

            // Scroll organically to trigger lazy loading
            await page.evaluate(async () => {
                const delay = ms => new Promise(resolve => setTimeout(resolve, ms));
                const scrollHeight = () => document.body.scrollHeight;
                let currentPosition = 0;
                const scrollStep = window.innerHeight; // Scroll by one viewport height at a time
                
                while (currentPosition < scrollHeight()) {
                    window.scrollBy(0, scrollStep);
                    currentPosition += scrollStep;
                    await delay(1000); // Give lazy loaders 1 second to load
                }
                
                // Scroll back to the top. This fixes floating headers overlapping content in the final shot.
                window.scrollTo(0, 0); 
                await delay(1000); 
            });

            // Wait for all remaining background requests (images, fonts) to finish
            console.log('Waiting for network to idle...');
            await page.waitForLoadState('networkidle', { timeout: 15000 }).catch(() => {
                console.log('Network idle timeout reached, proceeding anyway...');
            });

            // Capture full page
            const fileName = `${outDir}/${filePrefix}-${vp.name}.png`;
            await page.screenshot({ 
                path: fileName, 
                fullPage: true,
                animations: 'disabled' // Stops CSS animations from blurring the shot
            });
            
            console.log(`Saved: ${fileName}`);
        }
    }

    await browser.close();
    console.log('\nAll captures complete.');
})();
