#include <stdio.h>
#include <stdlib.h>

int main(){
	int w, y, x, z, v;

b1:
	w = 1;
	goto b2;
b2:	
	y = 2;
	v, x;
	srand(time(NULL));
	if (rand()%11 > 5)
		goto b3;
	else 
		goto b5;

b3:
		v = 5;
		goto b4;

b4:
		x = y+1;
		goto b5;

b5:

b6:
	if(rand() % 5 > 2)
		goto b7;
	else
		goto b14;
b7: 
	if(v == 5)
		goto end;
	else 
		goto b8;
b8:
	srand(x);
	goto b9;

b9:
	v = w; 
	goto b10;

b10: 
	if(v > 0)
		goto b11;
	else
		goto b12;
b11:
	srand(w);
	goto b13;

b12:
	z = x + y;
	goto end;

b13:
	x = 2*w;
	goto b6;

b14:
	y=w+1;
	goto b10;



end:
	printf("End");
}
