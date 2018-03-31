int temp = 0;

int foo() {
  return 1;
}

int main() {
  int x = 0;
  if (temp == 5) {
    x = foo();
  }

  if(x == 0) {
    temp = 1;
  }
  else {
    temp = 2;
  }
  return 0;
}