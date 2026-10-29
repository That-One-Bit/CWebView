import "./helper.js";
const name = "world";
let el = document.getElementById("target");
el.textContent = helper(name);
document.getElementById("styled").addEventListener("click", () => {
    console.log("activated es6 listener");
});
const nums = [1, 2, 3];
const total = nums.reduce((p, c) => p + c, 0);
console.log(`total is ${total}`);
const words = ["alpha", "beta"];
let joined = "";
for (const w of words) {
    joined += w[0];
}
console.log(`joined ${joined}`);
for (let ch of "ok") console.log("ch " + ch);

console.log("found " + [1, 2, 3].find((x) => x > 1));
console.log("padded [" + "ab".padStart(4, "0") + "]");
const m = Object.assign({ a: 1 }, { b: 2 });
console.log(`merged ${m.a} ${m.b}`);
const scores = new Map();
scores.set("alpha", 10);
scores.set("beta", 20);
console.log(`map ok ${scores.size}`);
const tags = new Set();
tags.add("x");
tags.add("x");
tags.add("y");
console.log(`set ok ${tags.size}`);
