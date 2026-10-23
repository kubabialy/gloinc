// Keep published directories immutable. Development is a moving preview;
// latestVersion selects the latest published version.
window.gloinDocs = {latestVersion: "0.2.1", versions: ["0.0.1", "0.0.2", "0.0.3", "0.0.4", "0.1.0", "0.2.0", "0.2.1", "development"]};

// Use an explicit file for offline archives. Capture before the inline handlers
// in published guides so those immutable pages also gain working navigation.
document.addEventListener("change", event => {
    const selector = event.target;
    if (!selector || selector.id !== "version" || !window.gloinDocs.versions.includes(selector.value)) return;
    event.stopImmediatePropagation();
    location.href = "../" + encodeURIComponent(selector.value) + "/index.html";
}, true);
