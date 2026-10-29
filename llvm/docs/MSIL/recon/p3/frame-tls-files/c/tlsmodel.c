extern _Thread_local int x;
static _Thread_local int y;
__attribute__((tls_model("local-exec"))) _Thread_local int z;
int f(void){return x+y+z;}
