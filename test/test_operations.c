#include <stdio.h>
int temp = 0;

int main() {
  fprintf(stdin, "hello");
  int x = 20;

  if (temp == 0) {
    x = x / 4;
  }
  else {
    x = x / 2;
  }

  if (x < 10) {
    return 0;
  }
  else {
    return 1;
  }

}
