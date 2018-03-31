int temp = 0;
int temp2 = 0;

int foo() {
  if (temp == 5) {
    return 0;
  }
  return temp;
}

int main() {

  int x = foo();

  if(temp2 == 0) {
    temp = 1;
    x = foo();
  }
  else {
    temp = 0;
    x = foo();
  }

  if (x == 0) {
    return 1;
  }
  else {
    return 0;
  }
}
