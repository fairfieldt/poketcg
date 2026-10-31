# Queue wakeup regression

The default harness runs 4,096 iterations of normal/high-priority queue wrapping, queued callbacks checked in order, fences and idle wakeups. It does not change timeout behavior or modify the installed SDK.

Build with `make ROM=rspq-wakeup-fixed WAKE_FIX=1 all` or `make ROM=rspq-wakeup-baseline WAKE_FIX=0 all`. The project-local fixed object is built by the adventure Makefile. For the same-layout negative control, run `tools/rspq_branch_control.py` from the adventure directory, then pass its object as `QUEUE_OBJECT` while keeping `WAKE_FIX=1`.

A bounded optional `FORCE_WINDOW=1` probe attempted to establish the exact RSP status-read race window using architectural single-step. Ares v148 did not honor the one-instruction step and reached PC 0x018 instead of the required PC 0x008 in both builds. That setup is unsupported there; its failed precondition does not evaluate the hotfix. The ordinary original/fixed/control stress tests all passed. See `port/reviews/rspq-wakeup/` for the actual results and limitations.
