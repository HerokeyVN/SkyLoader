#include "../02-list-collection/list-collection.cpp"

#include <cassert>
#include <limits>

Value call(const char* json) {
  Value request, response;
  assert(skyplugin::parse(json, &request));
  handleRequest(request, response);
  return response;
}

int main() {
  for (const char* json : {"1e309", "-1e309", "[1e999]", "{\"n\":1e309}"}) {
    Value value;
    assert(!skyplugin::parse(json, &value));
  }
  for (const char* json : {"1e308", "-1e308", "0", "0.5"}) {
    Value value;
    assert(skyplugin::parse(json, &value));
    assert(std::isfinite(value.number));
  }
  for (const char* json : {
      R"({"action":"remove","index":3})",
      R"({"action":"replace","index":3,"item":"new"})",
      R"({"action":"move","from":3,"to":0})",
      R"({"action":"move","from":0,"to":3})",
      R"({"action":"remove","index":-1})",
      R"({"action":"remove","index":0.5})",
      R"({"action":"remove","index":1e308})",
      R"({"action":"sort","order":"invalid"})"}) {
    g_items = {"a", "b", "c"};
    assert(!call(json).at("ok").boolean);
    assert((g_items == std::vector<std::string>{"a", "b", "c"}));
  }
  for (double index : {std::numeric_limits<double>::infinity(),
                       std::numeric_limits<double>::quiet_NaN()}) {
    Value request = Value::object(), response;
    request.set("action", Value("remove"));
    request.set("index", Value(index));
    handleRequest(request, response);
    assert(!response.at("ok").boolean);
  }
  g_items.clear();
  for (const char* json : {
      R"({"action":"remove","index":0})",
      R"({"action":"replace","index":0,"item":"new"})",
      R"({"action":"move","from":0,"to":0})"}) {
    assert(!call(json).at("ok").boolean);
    assert(g_items.empty());
  }
  g_items = {"a", "b", "c"};
  assert(call(R"({"action":"replace","index":2,"item":"z"})").at("ok").boolean);
  assert(g_items[2] == "z");
  assert(call(R"({"action":"move","from":2,"to":0})").at("ok").boolean);
  assert(g_items[0] == "z");
  assert(call(R"({"action":"remove","index":2})").at("ok").boolean);
  assert(g_items.size() == 2);
  assert(!sortOrderValid(Value("invalid"), nullptr));
}
