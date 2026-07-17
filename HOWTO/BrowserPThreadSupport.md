```html
<!doctype html>
<html lang="en">
<head>
    <meta charset="utf-8">
    <meta name="viewport" content="width=device-width, initial-scale=1">

    <title>SDL Application</title>

    <style>
        body {
            margin: 0;
            font-family: sans-serif;
        }

        #error {
            display: none;
            max-width: 640px;
            margin: 48px auto;
            padding: 24px;
            border: 1px solid #aaa;
        }

        #error-details {
            white-space: pre-wrap;
            font-family: monospace;
        }

        #canvas {
            width: 100vw;
            height: 100vh;
            display: block;
        }
    </style>
</head>

<body>
    <div id="error">
        <h2>Unable to start</h2>

        <p>
            This application requires WebAssembly pthread support.
            Use a current browser and make sure the server enables
            cross-origin isolation.
        </p>

        <div id="error-details"></div>
    </div>

    <canvas id="canvas"></canvas>

    <script>
        function getPthreadFailureReason() {
            if (typeof WebAssembly !== "object") {
                return "WebAssembly is not supported.";
            }

            if (typeof SharedArrayBuffer === "undefined") {
                return (
                    "SharedArrayBuffer is unavailable.\n" +
                    "The browser may be unsupported, or the page may not be " +
                    "cross-origin isolated."
                );
            }

            if (typeof Atomics === "undefined") {
                return "JavaScript Atomics are unavailable.";
            }

            if (window.crossOriginIsolated !== true) {
                return (
                    "The page is not cross-origin isolated.\n\n" +
                    "The server must return:\n" +
                    "Cross-Origin-Opener-Policy: same-origin\n" +
                    "Cross-Origin-Embedder-Policy: require-corp"
                );
            }

            return null;
        }

        function showStartupError(message) {
            document.getElementById("canvas").style.display = "none";
            document.getElementById("error").style.display = "block";
            document.getElementById("error-details").textContent = message;
        }

        function loadApplication() {
            const failureReason = getPthreadFailureReason();

            if (failureReason !== null) {
                showStartupError(failureReason);
                return;
            }

            window.Module = {
                canvas: document.getElementById("canvas"),

                onAbort(reason) {
                    showStartupError(
                        "The application aborted during startup:\n" +
                        String(reason)
                    );
                },

                onRuntimeInitialized() {
                    console.log("Emscripten runtime initialized.");
                }
            };

            const script = document.createElement("script");
            script.src = "game.js";

            script.onerror = function () {
                showStartupError(
                    "Failed to load game.js."
                );
            };

            document.body.appendChild(script);
        }

        loadApplication();
    </script>
</body>
</html>
```