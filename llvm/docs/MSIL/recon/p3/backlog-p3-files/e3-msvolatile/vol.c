volatile int flag;
volatile long long big;
int poll(void) { return flag; }
void set(int v) { flag = v; big = v; }
int main(void) { set(3); return poll() - 3; }
