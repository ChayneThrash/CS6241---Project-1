int temp = 0;

int foo() {
  return 1;
}

int main() {
  int x = 0;
  if (temp == 5) {
    x = foo();
  }

  if(x > 3) {
    if (x > 1) {
      temp = 1;
    }
  }
  else {
    temp = 2;
  }
  return 0;
}
