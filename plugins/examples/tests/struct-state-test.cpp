#include "../03-struct-state/struct-state.cpp"

#include <cassert>

Value call(const char* json) {
  Value request, response;
  assert(skyplugin::parse(json, &request));
  handleRequest(request, response);
  return response;
}

int main() {
  g_doc = defaults();
  const std::string original = skyplugin::write(g_doc);
  for (const char* json : {
      R"({"action":"patch","fields":{"profile.nickname":"changed","schedule.999":{}}})",
      R"({"action":"patch","fields":{"profile.nickname":"changed","profile.candles.invalid":1}})",
      R"({"action":"patch","fields":{"profile":null,"profile.nickname":"changed"}})"}) {
    assert(!call(json).at("ok").boolean);
    assert(skyplugin::write(g_doc) == original);
  }
  assert(call(R"({"action":"patch","fields":{"profile.nickname":"changed","settings.autoCollect":true}})").at("ok").boolean);
  assert(g_doc.at("profile").at("nickname").string == "changed");
  assert(g_doc.at("settings").at("autoCollect").boolean);
  assert(call(R"({"action":"set-path","path":"profile.nullable","value":null})").at("ok").boolean);
  const Value nullReply = call(R"({"action":"get-path","path":"profile.nullable"})");
  assert(nullReply.at("ok").boolean);
  assert(nullReply.at("data").contains("value"));
  assert(nullReply.at("data").at("value").isNull());
  assert(!call(R"({"action":"get-path","path":"profile.absent"})").at("ok").boolean);
  assert(!call(R"({"action":"get-path","path":"profile.nullable.child"})").at("ok").boolean);
  assert(!call(R"({"action":"schedule-remove","index":1e308})").at("ok").boolean);
}
