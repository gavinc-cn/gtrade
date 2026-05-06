# GTrade Landing Page Design

## Overview

A single-file HTML landing page for the GTrade project, deployed via GitHub Pages at `https://gavinc-cn.github.io/gtrade`. Dark tech aesthetic with Chinese-English bilingual content.

## Target Audience

- Technical community / developers
- Personal portfolio / resume showcase

## Visual Style

- Dark background (#0a0e17) with neon blue/green accents
- Terminal/dashboard feel
- CSS grid animations + gradient glow effects
- Responsive design (mobile + desktop)

## Page Structure

### 1. Hero Section
- Project name "GTrade" with large title
- Bilingual subtitle: "High-Performance Quantitative Crypto Trading Platform" / "高性能量化加密货币交易平台"
- Tech stack badges (C++17, Python, Vue 3, AI)
- GitHub repo link button
- Animated CSS grid background with gradient glow

### 2. Feature Cards (6 cards)
Each card has an SVG icon + bilingual title and description:
- Ultra-Low Latency Engine (C++17, SIMD, sub-microsecond)
- 3-Tier Strategy Isolation (in-process, subprocess, remote)
- High Availability (Primary/Standby, WAL, snapshot recovery)
- Professional Backtesting (CSV replay, orderbook simulation)
- Dynamic Plugin System (.so hot-loading, self-registering factories)
- AI-Native Integration (MCP protocol, AutoResearch pipeline)

Cards have hover glow effects, appear with scroll animation (Intersection Observer).

### 3. Architecture Diagram
CSS-rendered layered architecture: Client layer → API layer → Engine layer → Infrastructure layer.
ASCII-art inspired style for tech credibility.

### 4. Tech Stack Display
Layered display with icon badges:
- Core: C++17, CMake, Boost.Asio, spdlog, sonic_json, TA-Lib
- Network: WebSocket++, cpp-httplib, gRPC, ZMQ
- Storage: MySQL, ClickHouse, Shared Memory IPC
- Web: Vue 3, Element Plus, Flask, SQLAlchemy
- AI: FastMCP, Anthropic Claude, AutoResearch
- Deploy: Docker, Docker Compose

### 5. Strategy Ecosystem
- Built-in strategy types (Grid, SMA, Arbitrage, etc.)
- MCP toolset overview
- AutoResearch pipeline 3-step diagram

### 6. Footer
- GitHub link, License, copyright

## Technical Implementation

- Single `index.html` file (~800-1200 lines)
- Inline CSS + inline JS
- No external dependencies (SVG icons, system font stack or Google Fonts)
- Intersection Observer for scroll-triggered animations
- Responsive layout (CSS Grid + Flexbox)
- Bilingual: English primary, Chinese secondary

## Deployment

- Push to `gh-pages` branch
- Enable GitHub Pages in repo Settings → Pages → Source: `gh-pages` branch
- Public URL: `https://gavinc-cn.github.io/gtrade`
