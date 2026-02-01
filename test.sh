gcc scc.c -o scc
gcc sccas.c -o sccas

./scc -S sccas.s sccas.c
./sccas sccas.s -o sccas2
./sccas2 sccas.s -o sccas

./scc -S scc.s scc.c
./sccas scc.s -o scc2
./scc2 -S scc2.s scc.c
./sccas scc2.s -o scc

./scc -S scc.s scc.c
./sccas scc.s -o scc_stage1

./scc_stage1 -S scc_stage2.s scc.c
./sccas scc_stage2.s -o scc_stage2

./scc_stage2 -S scc_stage3.s scc.c
./sccas scc_stage3.s -o scc_stage3

diff scc.s scc_stage3.s

./scc_stage3 -S hello_stage3.s hello.c
./sccas hello_stage3.s -o hello_stage3

./hello_stage3

echo "--------------------------------"

echo "if you see:"
echo ""
echo "hello, world"
echo "hello"
echo ""
echo "above this message, then everything is working correctly"
