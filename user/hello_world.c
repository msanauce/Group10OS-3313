// A program demonstrating basic C programming concepts

#include "kernel/types.h"
#include "kernel/stat.h"
#include "user/user.h"

// Example 1: Variables and types
void
demo_variables(void)
{
  printf("\n=== Variables and Types ===\n");
  int x = 42;
  char c = 'A';
  
  printf("Integer x = %d\n", x);
  printf("Character c = %c\n", c);
}

// Example 2: Functions with parameters and return values
int
add_numbers(int a, int b)
{
  return a + b;
}

int
multiply(int a, int b)
{
  int result = a * b;
  return result;
}

void
demo_functions(void)
{
  printf("\n=== Functions ===\n");
  int sum = add_numbers(10, 20);
  int product = multiply(5, 7);
  
  printf("10 + 20 = %d\n", sum);
  printf("5 * 7 = %d\n", product);
}

// Example 3: Arrays
void
demo_arrays(void)
{
  printf("\n=== Arrays ===\n");
  int numbers[5] = {10, 20, 30, 40, 50};
  
  printf("Array elements:\n");
  for(int i = 0; i < 5; i++) {
    printf("  numbers[%d] = %d\n", i, numbers[i]);
  }
  
  // Calculate sum
  int sum = 0;
  for(int i = 0; i < 5; i++) {
    sum += numbers[i];
  }
  printf("Sum of array = %d\n", sum);
}

// Example 4: Structures
struct point {
  int x;
  int y;
};

struct person {
  char name[20];
  int age;
  int id;
};

void
demo_structs(void)
{
  printf("\n=== Structures ===\n");
  
  struct point p;
  p.x = 100;
  p.y = 200;
  printf("Point: (%d, %d)\n", p.x, p.y);
  
  struct person student;
  student.age = 20;
  student.id = 12345;
  printf("Person: age=%d, id=%d\n", student.age, student.id);
}

// Example 5: Strings (character arrays)
void
demo_strings(void)
{
  printf("\n=== Strings ===\n");
  
  char greeting[] = "Hello";
  char name[] = "xv6";  // Need 4 chars: 'x', 'v', '6', '\0'
  
  printf("Greeting: %s\n", greeting);
  printf("Name: %s\n", name);
  
  // String length
  int len = strlen(name);
  printf("Length of '%s' = %d\n", name, len);
  
  // String concatenation (manual)
  char message[50] = "Welcome to ";
  int i = strlen(message);
  int j = 0;
  while(name[j] != '\0') {
    message[i++] = name[j++];
  }
  message[i] = '\0';
  printf("Message: %s\n", message);
}

// Example 6: Pointers
void
demo_pointers(void)
{
  printf("\n=== Pointers ===\n");
  
  int a = 5;           
  // a regular integer, stored somewhere in memory
  printf("a = %d\n", a);
  
  int *p = &a;         
  // a pointer to an integer value, `p` stores the memory location of `a`
  printf("p = %p (address of a)\n", p);
  printf("*p = %d (value at address p)\n", *p);
  
  *p = 6;              
  // when outside of declarations, * is a 'dereference' operator, i.e., give me the content in the address that variable p refers to
  printf("After *p = 6:\n");
  printf("a = %d (changed via pointer)\n", a);
  
  int **x = &p;        
  // a pointer to a pointer, `x` stores the memory location of `p`
  
  printf("x = %p (address of p)\n", x);
  printf("*x = %p (value at x, which is address of a)\n", *x);
  printf("**x = %d (value at address stored in p)\n", **x);
}

// Example 7: File I/O - Reading a file
void
demo_file_read(char *filename)
{
  printf("\n=== File Reading ===\n");
  
  char buf[512];
  int fd, n;
  
  // Open the file for reading
  fd = open(filename, 0);
  if(fd < 0){
    printf("Error: cannot open %s\n", filename);
    return;
  }
  
  printf("Reading from %s:\n", filename);
  
  // Read and print file contents
  while((n = read(fd, buf, sizeof(buf))) > 0) {
    write(1, buf, n);  // Write to stdout (fd = 1)
  }
  
  // Close the file
  close(fd);
  printf("\n");
}

int
main(int argc, char *argv[])
{
  printf("=== Basic C Programming Examples ===\n");
  
  demo_variables();
  demo_functions();
  demo_arrays();
  demo_structs();
  demo_strings();
  demo_pointers();
  if(argc < 2){
    printf("Usage: hello_world <filename>\n");
    exit(1);
  }
  demo_file_read(argv[1]);
  
  printf("\n=== All demos complete! ===\n");
  exit(0);
}


