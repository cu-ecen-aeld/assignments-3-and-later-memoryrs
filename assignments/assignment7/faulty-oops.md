# OOPS Message Analysis
Run `echo “hello_world” > /dev/faulty` command resulting the following message. Analysis is provided below.
## OOPS Message
```
Unable to handle kernel NULL pointer dereference at virtual address 0000000000000000
Mem abort info:
  ESR = 0x0000000096000045
  EC = 0x25: DABT (current EL), IL = 32 bits
  SET = 0, FnV = 0
  EA = 0, S1PTW = 0
  FSC = 0x05: level 1 translation fault
Data abort info:
  ISV = 0, ISS = 0x00000045
  CM = 0, WnR = 1
user pgtable: 4k pages, 39-bit VAs, pgdp=0000000041b6a000
[0000000000000000] pgd=0000000000000000, p4d=0000000000000000, pud=0000000000000000
Internal error: Oops: 0000000096000045 [#1] SMP
Modules linked in: hello(O) faulty(O) scull(O)
CPU: 0 PID: 151 Comm: sh Tainted: G           O       6.1.44 #1
Hardware name: linux,dummy-virt (DT)
pstate: 80000005 (Nzcv daif -PAN -UAO -TCO -DIT -SSBS BTYPE=--)
pc : faulty_write+0x10/0x20 [faulty]
lr : vfs_write+0xc8/0x390
sp : ffffffc008dfbd20
x29: ffffffc008dfbd80 x28: ffffff8001b35cc0 x27: 0000000000000000
x26: 0000000000000000 x25: 0000000000000000 x24: 0000000000000000
x23: 0000000000000012 x22: 0000000000000012 x21: ffffffc008dfbdc0
x20: 000000558db791e0 x19: ffffff8001bc3900 x18: 0000000000000000
x17: 0000000000000000 x16: 0000000000000000 x15: 0000000000000000
x14: 0000000000000000 x13: 0000000000000000 x12: 0000000000000000
x11: 0000000000000000 x10: 0000000000000000 x9 : 0000000000000000
x8 : 0000000000000000 x7 : 0000000000000000 x6 : 0000000000000000
x5 : 0000000000000001 x4 : ffffffc000787000 x3 : ffffffc008dfbdc0
x2 : 0000000000000012 x1 : 0000000000000000 x0 : 0000000000000000
Call trace:
 faulty_write+0x10/0x20 [faulty]
 ksys_write+0x74/0x110
 __arm64_sys_write+0x1c/0x30
 invoke_syscall+0x54/0x130
 el0_svc_common.constprop.0+0x44/0xf0
 do_el0_svc+0x2c/0xc0
 el0_svc+0x2c/0x90
 el0t_64_sync_handler+0xf4/0x120
 el0t_64_sync+0x18c/0x190
Code: d2800001 d2800000 d503233f d50323bf (b900003f) 
---[ end trace 0000000000000000 ]---
```

## Analysis
`Unable to handle kernel NULL pointer dereference at virtual address 0000000000000000` is caused by the kernel attempting to dereference a NULL pointer.  

The mem abort info and data abort info specify the register information at the time of the fault. 
`CPU: 0 PID: 151 Comm: sh Tainted: G     O  6.1.44 #1` indicates the CPU on which the fault occurred; the PID and process name causing the OOPS with the PID and Comm field; and the Tainted: 'G' flag specifies that the kernel was tainted by loading a proprietary module.

`pc : faulty_write+0x10/0x20 [faulty]` provides the program counter value at the time of the oops. According to this, we can deduce that the oops message occurred when executing the faulty_write function. The byte offset at which the crash occurred is 0x14 or 20 bytes into the function while the length of the function is denoted by 0x20 or 32 bytes. The subsequent lines give info on the Link register and stack pointer contents, followed by the CPU register contents and the call trace.

The `Call trace` section provides the list of functions that were called before the oops occurred.

`Code: d2800001 d2800000 d503233f d50323bf (b900003f)` provides a hex-dump of the machine code that was executing at the time the oops occurred. 
