// Host-Testlauf fuer LeviLuma Next (Protokoll, Gateway- und Controller-Logik).
#include "test_util.h"

int g_checks = 0, g_failures = 0;

int main() {
  printf("== Protokoll ==\n");
  tests_protocol();
  printf("== Zuverlaessigkeit ==\n");
  tests_link();
  printf("== Gateway ==\n");
  tests_gateway();
  printf("== Controller ==\n");
  tests_controller();
  printf("\n%d Pruefungen, %d Fehler\n", g_checks, g_failures);
  return g_failures != 0;
}
