import { render } from "preact";
import { App } from "./app";

// Register PWA service worker (enables install prompt in Chrome/Edge)
if ("serviceWorker" in navigator) {
  navigator.serviceWorker.register(
    new URL("./sw.js", import.meta.url),
    { scope: "./" }
  ).catch(() => {
    // Service worker registration failed — PWA install may be unavailable
  });
}

render(<App />, document.getElementById("root")!);
