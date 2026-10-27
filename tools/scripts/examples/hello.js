function square(value) {
  return value * value;
}
for (var i = 1; i <= 5; i++) {
  console.log("square", i, square(i));
  sleep(100);
}
print(JSON.stringify({language: "JavaScript", finished: true}));
