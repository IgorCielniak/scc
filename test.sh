gcc scc.c -o scc

./scc -S scc.s scc.c

nasm scc.s -felf64 -o scc.o

gcc -no-pie scc.o -o scc_stage1

./scc_stage1 -S scc_stage2.s scc.c

nasm scc_stage2.s -felf64 -o scc_stage2.o

gcc -no-pie scc_stage2.o -o scc_stage2

./scc_stage2 -S scc_stage3.s scc.c

nasm scc_stage3.s -felf64 -o scc_stage3.o

gcc -no-pie scc_stage3.o -o scc_stage3

diff scc.s scc_stage3.s

./scc_stage3 -S hello_stage3.s hello.c

nasm hello_stage3.s -felf64 -o hello_stage3.o

gcc -no-pie hello_stage3.o -o hello_stage3

./hello_stage3

echo "--------------------------------"

echo "if you see:"
echo ""
echo "hello, world"
echo "hello"
echo ""
echo "above this message, then everything is working correctly"
