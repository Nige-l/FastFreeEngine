# Build Your Own Engine

**Want to understand how game engines really work? Build one.**

This learning track walks you through building simplified versions of real engine subsystems from scratch. You will not be using FastFreeEngine's code -- you will be writing your own, one piece at a time, in plain C++. By the end of each installment, you will have a working mini-system that you wrote and understand completely.

The goal is not to replace FFE. The goal is to make you the kind of developer who *could* build an engine -- and who understands exactly what FFE is doing under the hood when you use it to make your game.

---

## Who This Is For

You are a motivated teenager, a student, or a self-taught programmer who:

- Knows basic C++ (variables, functions, structs, templates are not scary)
- Has a compiler installed (GCC, Clang, or MSVC all work)
- Wants to understand *why* things work, not just *how* to use them
- Is curious about what happens between your game code and the pixels on screen

You do not need to know anything about game engines yet. That is what we are here for.

---

## The Series

Each installment is self-contained. You can do them in order or jump to whichever topic interests you most.

| # | Installment | Status | What You Build |
|---|------------|--------|----------------|
| 1 | [Build an ECS from Scratch](ecs-from-scratch.md) | Available | A mini Entity Component System in ~150 lines of C++ |
| 2 | Build a Sprite Renderer from Scratch | Coming soon | A 2D renderer that batches draw calls and talks to the GPU |
| 3 | Build a Physics Engine from Scratch | Coming soon | Collision detection and response for 2D shapes |
| 4 | Build a Networking Layer from Scratch | Coming soon | Client-server architecture with UDP sockets |

More installments will be added as FFE grows. If there is a subsystem you want to understand, [open an issue](https://github.com/user/FastFreeEngine/issues) and let us know.

---

## Prerequisites

- **A C++ compiler** that supports C++20. GCC 13+, Clang 16+, or MSVC 2022+ all work. If you are not sure what you have, open a terminal and run `g++ --version` or `clang++ --version`.
- **A text editor.** Anything works -- VS Code, Vim, Notepad++, whatever you like.
- **Curiosity.** Seriously, that is the most important one.

If you want to set up the full FastFreeEngine development environment (to compare your code against the real engine), see the [Getting Started](../getting-started.md) guide.

---

## How These Tutorials Work

Each installment follows the same structure:

1. **What you will build** -- a clear description of the finished product
2. **Why it matters** -- the problem this subsystem solves and why it is designed the way it is
3. **Step-by-step construction** -- you write code one piece at a time, with explanations at every step
4. **The complete program** -- all the code together, ready to copy into a single `.cpp` file and compile
5. **How FFE does it** -- a brief comparison to the real engine, so you can see how production code differs from a teaching example
6. **Challenges** -- extensions you can try on your own to push your understanding further

All code in these tutorials is **self-contained**. You do not need FFE installed. You do not need any libraries. Just a compiler and the code on the page.

---

## Ready?

Start with [Build an ECS from Scratch](ecs-from-scratch.md) -- it is the foundation everything else is built on.
