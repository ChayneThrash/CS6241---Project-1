int temp = 0;

int foo() {
  if (temp == 5) {
    return 0;
  }
  return temp;
}

int main() {
  if (temp) {
    temp = 5;
  }
  else {
    temp = 1;
  }
  
  int x = foo();

  if(x == 0) {
    temp = 1;
  }
  else {
    temp = 2;
  }
  return 0;
}
