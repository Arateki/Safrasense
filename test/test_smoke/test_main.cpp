#include <Arduino.h>
#include <unity.h>
#include <Preferences.h>

void test_sanity() { TEST_ASSERT_EQUAL(4, 2 + 2); }
void test_nvs_disponivel() {
  // Prova que o ambiente QEMU dá NVS real aos testes.
  Preferences p;
  TEST_ASSERT_TRUE(p.begin("t_smoke", false));
  p.putInt("x", 42);
  TEST_ASSERT_EQUAL(42, p.getInt("x", 0));
  p.end();
}

void setup() {
  delay(200);
  UNITY_BEGIN();
  RUN_TEST(test_sanity);
  RUN_TEST(test_nvs_disponivel);
  UNITY_END();
}
void loop() {}
