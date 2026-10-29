// Native standard DOM selections
const out = document.getElementById("out");
let clicks = 0;

/* template literal + Map, filled with for..of */
const names = ["alpha", "beta", "gamma"];
const lookup = new Map();

let i = 0;
for (const n of names) {
  i++;
  lookup.set(n, i);
}
console.log(`map has ${lookup.size} entries`);

// Click handler module logic using standard Arrow Functions
document.getElementById("inc").addEventListener("click", () => {
  clicks++;
  document.getElementById("count").textContent = `Clicks: ${clicks}`;
  out.style.cssText = "color:#34d399";
});

document.getElementById("scan").addEventListener("click", () => {
  const found = names.find((n) => n === "beta");
  let line = "";
  for (const ch of found) {
    line += ch + " ";
  }
  
  out.textContent = `beta is #${lookup.get(found)}, chars: ${line}`;
  console.log(`scan: ${found} is ${lookup.get(found)}`);
  
  const seen = new Set();
  for (const n of names) {
    seen.add(n);
  }
  console.log(`set size ${seen.size}`);
});

document.getElementById("reset").addEventListener("click", () => {
  clicks = 0;
  document.getElementById("count").textContent = "Clicks: 0";
  out.style.cssText = "color:#f8fafc";
  out.textContent = "output appears here";
  console.log("reset");
});

// Optional: Explicitly export a module initialization block if needed by your engine wrapper
export function initApp() {
  console.log("Vita WebView demo module initialized successfully.");
}
initApp();
