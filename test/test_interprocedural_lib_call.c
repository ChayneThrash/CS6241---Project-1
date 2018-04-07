#include <stdio.h>
int temp = 0;

void foo() {
 return;
}

int main() {
	foo();
  if (temp > 5) {
    temp = 5;
  }
  else {
    temp = 20;
  }
  temp = 5;
  printf(temp);

	int y = 5 * temp;
  return 0;
}
