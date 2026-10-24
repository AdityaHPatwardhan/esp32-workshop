// Edit before the workshop: names and links.
window.CONFIG = {
  hosts: [
    // linkedin: full profile URL; both are shown on the Thank you slide
    { name: "Aditya Patwardhan", org: "Espressif Systems", linkedin: "https://www.linkedin.com/in/aditya-patwardhan-a9a052136/", email: "aditya.patwardhan@espressif.com" },
    { name: "Harsh Mutha",       org: "Intangles",         linkedin: "https://www.linkedin.com/in/harsh-mutha/", email: "mutha.harsh@gmail.com" },
  ],
  college:    "Pune Institute of Computer Technology",
  date:       "",                 // optional, e.g. "14 November 2026"
  // ESP Launchpad links for the workshop binaries (see tools/launchpad/README.md)
  // One link for all three apps; Launchpad picks the ESP32 or ESP32-S3 build by the chip it finds
  launchpad:  "https://espressif.github.io/esp-launchpad/?flashConfigURL=https://adityahpatwardhan.github.io/esp32-workshop-flash/workshop.toml?v=3",
  repo:       "github.com/AdityaHPatwardhan/esp32-workshop",
  wifiSsid:   "esp-workshop",
  dashboard:  "http://<your-laptop-ip>:8080",
  // The "Live" slide embeds the dashboard running on this laptop
  dashboardLocal: "http://127.0.0.1:8080",
};
