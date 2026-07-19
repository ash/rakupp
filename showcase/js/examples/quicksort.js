// Two classic sorts, side by side, both non-destructive.

// Quicksort in the functional style: partition around a pivot with filter.
function quicksort(arr) {
  if (arr.length <= 1) { return arr; }
  const pivot = arr[0];
  const rest = arr.slice(1);
  const less = rest.filter(x => x < pivot);
  const more = rest.filter(x => x >= pivot);
  return quicksort(less).concat([pivot]).concat(quicksort(more));
}

// Mergesort with an explicit merge of two sorted runs.
function mergesort(arr) {
  if (arr.length <= 1) { return arr; }
  const mid = Math.floor(arr.length / 2);
  const left = mergesort(arr.slice(0, mid));
  const right = mergesort(arr.slice(mid));
  const out = [];
  let i = 0, j = 0;
  while (i < left.length && j < right.length) {
    if (left[i] <= right[j]) { out.push(left[i++]); }
    else { out.push(right[j++]); }
  }
  while (i < left.length) { out.push(left[i++]); }
  while (j < right.length) { out.push(right[j++]); }
  return out;
}

const data = [5, 2, 9, 1, 7, 3, 8, 4, 6, 0];
const show = a => a.join(" ");
console.log("input:    ", show(data));
console.log("quicksort:", show(quicksort(data)));
console.log("mergesort:", show(mergesort(data)));
console.log("unchanged:", show(data));

// The built-in sort needs a numeric comparator, or it sorts as strings.
console.log("as strings:", show(data.slice().sort()));
console.log("as numbers:", show(data.slice().sort((a, b) => a - b)));
