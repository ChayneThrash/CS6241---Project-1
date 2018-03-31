int temp = 0;

int foo() {
  if (temp == 5) {
    return 1;
  }
  else {
    return 0;
  }
}

int main() {
  int x = foo();

  if(x == 0) {
    temp = 1;
  }
  else {
    temp = 2;
  }
  return 0;
}
