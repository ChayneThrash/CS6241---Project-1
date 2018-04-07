#include <string.h>

int main() {
  const char* x = "hello";
  if (strcmp(x, "abc")) {
    return 0;
  }
  else {
    return 1;
  }
}