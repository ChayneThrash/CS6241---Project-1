#include <stdio.h>
int temp = 0;

int main() {
  fprintf(stdin, "hello");
  int x = 5;

  if (temp == 0) {
    x += 3;
  }
  else {
    x += 1;
  }

  if (x > 6) {
    return 0;
  }
  else {
    return 1;
  }

}
