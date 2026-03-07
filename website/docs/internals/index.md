# How It Works

Ever wondered what happens inside a game engine? This section explains the internals of FastFreeEngine -- how the systems are designed, why they work the way they do, and what trade-offs were made along the way.

These pages are written for curious developers and students who want to understand game engine architecture, not just use it. You do not need to read this section to build games with FFE, but if you want to learn how engines work under the hood, this is the place.

Each topic starts with the problem being solved, explains the design and trade-offs in plain language, and ends with a thought experiment that shows why the chosen approach beats the alternatives. Code snippets illustrate concepts -- they are not step-by-step tutorials. For that, see the [Tutorials](../tutorials/index.md) section.

## Deep Dives

<div class="grid cards" markdown>

-   **[Entity Component System](ecs.md)**

    How FFE organizes game objects using entities, components, and systems. Why arrays beat linked lists, why function pointers beat virtual functions, and how to create your own components.

-   **[The Renderer](renderer.md)**

    How pixels get on screen efficiently on old hardware. The 2D sprite batching pipeline, 3D Blinn-Phong shading, shadow mapping, skeletal animation, and why draw call count is the metric that matters.

-   **[Multiplayer Networking](networking.md)**

    How FFE makes games feel responsive over unreliable networks. Client-server architecture, snapshot replication, delta compression, client-side prediction, and lag compensation.

</div>

## Coming Soon

- **Audio System** -- How sound effects and music are mixed and played
- **Physics Engine** -- How collision detection and spatial hashing work
- **Editor Architecture** -- How the standalone editor application is built

Each topic is adapted from the engine's internal architecture decision records (ADRs), rewritten for a general audience.
