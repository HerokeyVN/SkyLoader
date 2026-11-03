const assert = require("node:assert/strict");
const fs = require("node:fs");
const path = require("node:path");
const vm = require("node:vm");
const { test } = require("node:test");

function page(sample, call) {
  const elements = new Map();
  function element() {
    return {
      value: "", textContent: "", checked: false, children: [],
      classList: { add() {}, remove() {} }, addEventListener() {},
      appendChild(child) { this.children.push(child); },
      prepend(child) { this.children.unshift(child); },
      removeChild() { this.children.shift(); },
    };
  }
  function select(id) {
    if (!elements.has(id)) elements.set(id, element());
    return elements.get(id);
  }
  const context = vm.createContext({
    document: {
      querySelector: select,
      getElementById: (id) => select("#" + id),
      createElement: element,
      createTextNode: (textContent) => ({ textContent }),
    },
    window: { sky: { call, onEvent() {}, plugin: { id: "test", version: "1" } } },
  });
  vm.runInContext(fs.readFileSync(path.join(__dirname, "..", sample, "ui/app.js"), "utf8"), context);
  return { select, run: (code) => vm.runInContext(code, context) };
}

test("volume input saves its numeric value and clamps invalid ranges", async () => {
  let saved;
  const fields = { nickname: "Sky", theme: "auto", volume: 50, sensitivity: 1, notifications: true };
  const ui = page("01-field-settings", async (request) => {
    if (request.action === "set") saved = request.fields;
    return { ok: true, data: fields };
  });
  await new Promise(setImmediate); // finish the page's initial load
  ui.select("#volume").value = "67";
  await ui.run("saveChanges()");
  assert.equal(saved.volume, 67);
  for (const [input, expected] of [["50", 50], ["12.6", 13], ["-10", 0], ["150", 100], ["NaN", 0], ["Infinity", 0]]) {
    assert.equal(ui.run(`volumeOf(${JSON.stringify(input)})`), expected);
  }
});

test("schedule and path mutations reconcile state and rendered controls", async () => {
  const doc = { profile: { nickname: "Sky" }, settings: {}, schedule: [] };
  let gets = 0;
  let reject = false;
  const ui = page("03-struct-state", async (request) => {
    if (reject) return { ok: false, error: { code: "REJECTED", message: "rejected" } };
    let data;
    if (request.action === "get") { gets++; data = doc; }
    if (request.action === "schedule-add") {
      doc.schedule.push({ when: request.when, what: request.what });
      data = doc.schedule;
    }
    if (request.action === "schedule-remove") { doc.schedule.splice(request.index, 1); data = doc.schedule; }
    if (request.action === "set-path") {
      doc.profile.nickname = request.value;
      data = { path: request.path, value: request.value };
    }
    if (request.action === "get-path") data = { path: "profile.nullable", value: null };
    // The bridge transfers JSON, so state must not share backend references.
    return { ok: true, data: JSON.parse(JSON.stringify(data)) };
  });
  await new Promise(setImmediate);
  await ui.run('send("schedule-add", {when:"10:00", what:"Run"})');
  assert.equal(ui.run("state.doc.schedule.length"), 1);
  assert.equal(ui.select("#schedule-stats").textContent, "(1 items)");
  await ui.run('send("schedule-remove", {index:0})');
  assert.equal(ui.run("state.doc.schedule.length"), 0);
  assert.equal(ui.select("#schedule-stats").textContent, "(0 items)");
  assert.equal(gets, 1); // schedule arrays reconcile without a second fetch
  const reply = await ui.run('send("set-path", {path:"profile.nickname", value:"Updated"})');
  assert.equal(reply.value, "Updated");
  assert.equal(ui.run("state.doc.profile.nickname"), "Updated");
  assert.equal(ui.select("#nickname").value, "Updated");
  assert.equal(gets, 2);
  const nullReply = await ui.run('send("get-path", {path:"profile.nullable"})');
  assert.equal(nullReply.value, null);
  assert.equal(ui.run("state.doc.profile.nickname"), "Updated");
  reject = true;
  await assert.rejects(ui.run('send("schedule-add", {when:"bad", what:"bad"})'));
  assert.equal(ui.run("state.doc.schedule.length"), 0);
});
