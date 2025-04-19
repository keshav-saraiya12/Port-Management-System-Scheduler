# w5yxe7de-Port-Management-System-Scheduler
Port Management System Scheduler - Operating System
Designed and implemented a real-time Port Management System Scheduler simulating ship scheduling, cargo management, and dock coordination using POSIX IPC mechanisms.

Efficiently handled 500+ concurrent ship docking requests (regular, emergency, outgoing) using priority scheduling.

Managed shared memory and multiple message queues (main and solver-specific) for inter-process communication with strict timing and concurrency rules.

Implemented dock assignment logic, crane-based cargo loading/unloading (with constraints), and undocking via solver-assisted frequency guessing mechanism.

Optimized the scheduler to pass all validation testcases under strict real-time and simulated timestep limits (≤ 6 mins, ≤ 600 timesteps).

Achieved 100% correctness across all six validation scenarios, maintaining synchronicity and rule adherence in a multi-process environment.

Skills Used: C Programming, POSIX IPC (Shared Memory, Message Queues), Concurrent Programming, Synchronization, Systems Programming, Optimization under Constraints.
