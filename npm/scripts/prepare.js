// Before npm publish: the license from the repository into the package
const fs = require("fs");
const path = require("path");
fs.copyFileSync(path.join(__dirname, "..", "..", "LICENSE"), path.join(__dirname, "..", "LICENSE"));
console.log("copied LICENSE");
