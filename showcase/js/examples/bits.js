// Bitwise operators, switch, and optional chaining — a tiny RGB colour tool.

// Pack r,g,b (0..255) into one 24-bit integer with shifts and OR.
function rgb(r, g, b) {
  return ((r & 0xFF) << 16) | ((g & 0xFF) << 8) | (b & 0xFF);
}

// Pull the channels back out with shift + mask.
function channels(c) {
  return {
    r: (c >> 16) & 0xFF,
    g: (c >> 8) & 0xFF,
    b: c & 0xFF,
  };
}

const orange = rgb(255, 140, 0);
console.log("packed:", orange, "hex:", "#" + orange.toString(16));
console.log("channels:", JSON.stringify(channels(orange)));

// A switch classifies a channel value, with fall-through grouping the top band.
function level(v) {
  switch (v >> 6) {          // 0..3
    case 0:
      return "dark";
    case 1:
    case 2:
      return "mid";
    default:
      return "bright";
  }
}
for (const v of [10, 90, 200, 255]) {
  console.log(v, "->", level(v));
}

// Optional chaining walks a possibly-missing palette without throwing.
const theme = {
  palette: { primary: { hex: "#ff8c00" } },
};
console.log(theme?.palette?.primary?.hex);
console.log(theme?.palette?.secondary?.hex ?? "(no secondary)");
console.log(theme?.missing?.deep?.value ?? "(absent)");

// XOR round-trips a byte against a key — encrypt, then decrypt back.
const key = 0x5A;
const msg = [72, 105];                      // "Hi"
const enc = msg.map(b => b ^ key);
const dec = enc.map(b => b ^ key);
console.log("encoded:", enc.join(","), "decoded:", dec.map(c => String.fromCharCode(c)).join(""));
