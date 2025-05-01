# LC-3 VM (2048 Game)

This project implements a minimal LC-3 virtual machine in C, capable of running LC-3 object files. This project includes the LC-3 version of the 2048 game by [Robert Pendleton](https://github.com/rpendleton), originally published at [github.com/rpendleton/lc3-2048](https://github.com/rpendleton/lc3-2048) under the MIT License.

## 🕹️ How to Run the Game

### 1. Compile the LC-3 VM
Use `clang` or `gcc` to compile the VM:

```bash
clang -g main.c -o lc3-vm
# or
gcc -g main.c -o lc3-vm
```

### 2. Run the VM with the 2048.obj file (courtesy of [Robert Pendleton](https://github.com/rpendleton))

```bash
./lc3-vm 2048.obj
```

When you run the program, you will see:

Control the game using WASD keys.
Are you on an ANSI terminal (y/n)? 

Type y and press Enter.
The 2048 game will launch inside your terminal.