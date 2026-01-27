# PPB CLI - Put Binary

A lightweight command-line tool for uploading content to PPB servers. Written in C with cURL for maximum portability and performance.

## About

This is a simple solution for quickly sharing text, code, logs, or any file content via HTTP. By default, it uploads to **epa.st** (a courtesy public instance), but you're **strongly encouraged to host your own server**. The public instance may have rate limits, downtime, or may not be available long-term.

**Self-hosting is easy** - see the [server documentation](../ppb-server/README.md) for setup instructions.

## Features

- 📥 Upload from stdin or pipe output directly
- 🏠 Self-hostable server (recommended)
- 🔧 Multiple server configurations
- 🔐 Token-based authentication
- ⚙️ Flexible configuration (CLI args, env vars, config files)
- 🚀 Fast and lightweight (~50KB binary)
- 🔒 Secure by default (600 permissions on config)

## Quick Start

### Prerequisites

- C compiler (clang or gcc)
- libcurl development files
- make

**Install dependencies:**

```bash
# macOS
brew install curl

# Ubuntu/Debian
sudo apt-get install libcurl4-openssl-dev

# Fedora/RHEL
sudo dnf install libcurl-devel

# Arch Linux
sudo pacman -S curl
```

### Build

Building is super simple:

```bash
cd ppb-cli
make
```

That's it! This will produce a `put` binary in the current directory.

### Install

To install to your local bin directory:

```bash
make install
```

This copies the binary to `~/.local/bin/put`. Make sure `~/.local/bin` is in your PATH:

```bash
# Add to ~/.bashrc, ~/.zshrc, or ~/.config/fish/config.fish
export PATH="$HOME/.local/bin:$PATH"
```

### Clean

To remove build artifacts:

```bash
make clean
```

## Usage

### Basic Upload

Pipe any content directly to `put`:

```bash
echo "Hello, world!" | put
```

Read from a file:

```bash
cat myfile.txt | put
```

Upload command output:

```bash
ls -la | put
curl -s https://api.github.com/users/octocat | put
```

### Configuration

`put` uses a flexible configuration system with the following precedence (highest to lowest):

1. **Command-line arguments** (highest priority)
2. **Environment variables**
3. **Config file**
4. **Defaults** (lowest priority)

#### Command-Line Options

```bash
Options:
  --url <URL>          Override server URL
  --token <TOKEN>      Override auth token
  --server <NAME>      Use server config by name
  --config <PATH>      Use custom config file
  --init-config        Write default config then exit
  -v, --verbose        Verbose output
  -r, --response       Show full server response
  -h, --help           Show help message
```

**Examples:**

```bash
# Use a specific server
echo "test" | put --url https://example.com/upload --token YOUR_TOKEN

# Show verbose output
echo "test" | put -v

# Show full JSON response
echo "test" | put --response

# Use a named server from config
echo "test" | put --server prod
```

#### Environment Variables

```bash
export PPB_URL="https://your-server.com/upload"
export PPB_TOKEN="your-auth-token-here"

echo "test" | put
```

#### Config File

`put` automatically looks for config files in this order:

1. `.ppb-config.json` (current directory)
2. `~/.ppb/config.json` (home directory)

**Create a config file:**

```bash
put --init-config
```

This creates `~/.ppb/config.json` with a default structure:

```json
{
  "default_server": "https://epa.st/upload",
  "default_token": "",
  "servers": {
    "local": {
      "url": "http://127.0.0.1:8000/upload",
      "token": ""
    },
    "prod": {
      "url": "https://your-domain.com/upload",
      "token": "your-token-here"
    }
  }
}
```

> **Note:** `epa.st` is provided as a courtesy default but may not always be available. For reliable usage, **host your own server** and update the config with your URL and token.

**Using named servers:**

After setting up your config, you can easily switch between servers:

```bash
# Use production server
echo "Hello" | put --server prod

# Use local development server
echo "Testing" | put --server local
```

### Getting a Token

#### For Self-Hosted Servers (Recommended)

Generate a token from your own server:

```bash
curl -X POST https://your-domain.com/token
```

#### For epa.st (Public Instance)

You can request a token for the public instance, but **this is not guaranteed to always be available**:

```bash
curl -X POST https://epa.st/token
```

Then add it to your config file or environment:

```bash
# Option 1: Add to config file
nano ~/.ppb/config.json

# Option 2: Export as environment variable
export PPB_TOKEN="your-token-here"

# Option 3: Pass directly on command line
echo "test" | put --token your-token-here
```

## Complete Examples

### Basic workflow

```bash
# Generate a token from your server
curl -X POST https://ppb.example.com/token
# {"token": "abc123xyz..."}

# Add to config
put --init-config
nano ~/.ppb/config.json
# Add your URL and token

# Upload something
echo "Hello from CLI" | put
# https://ppb.example.com/raw/a1b2c3d4e5f6g7h8

# Upload a file
cat script.sh | put -r
# {
#   "meta": {
#     "created_at": 1706313600,
#     "size": 1234,
#     "checksum": "abc123...",
#     "short": "abc123..."
#   },
#   "url": "https://ppb.example.com/raw/abc123..."
# }
```

### Multi-server setup

```bash
# Set up multiple servers in config
cat > ~/.ppb/config.json << 'EOF'
{
  "default_server": "https://ppb.example.com/upload",
  "default_token": "prod-token-here",
  "servers": {
    "local": {
      "url": "http://localhost:8000/upload",
      "token": "local-dev-token"
    },
    "staging": {
      "url": "https://staging.ppb.example.com/upload",
      "token": "staging-token"
    },
    "prod": {
      "url": "https://ppb.example.com/upload",
      "token": "prod-token"
    }
  }
}
EOF

# Test on local
echo "local test" | put --server local

# Deploy to staging
cat deployment.log | put --server staging

# Share on production
cat public-script.sh | put --server prod
```

### Shell integration

Add to your `.bashrc`, `.zshrc`, or `.config/fish/config.fish`:

```bash
# Quick paste alias
alias paste='put'

# Paste with URL copied to clipboard (macOS)
alias pastec='put | tee /dev/tty | pbcopy'

# Paste with URL copied to clipboard (Linux with xclip)
alias pastec='put | tee /dev/tty | xclip -selection clipboard'
```

### Real-world examples

```bash
# Share your public SSH key
cat ~/.ssh/id_rsa.pub | put

# Share system information
uname -a | put

# Share a code snippet with syntax highlighting info
cat mycode.py | put

# Share command output for debugging
docker ps -a | put
journalctl -xe | put

# Share a curl response
curl -s https://api.github.com/users/github | put

# Chain with other commands
git diff | put && git add .
```

## Build Details

### What it includes

- **put.c** - Main CLI source code
- **vendor/cJSON.c** & **vendor/cJSON.h** - Embedded JSON parser (no external deps)
- **Makefile** - Simple build configuration

### Compiler flags

- `-Wall -Wextra -Werror` - All warnings enabled, warnings are errors
- `-O2` - Optimization level 2 for performance
- `-std=c99` - C99 standard
- `-lcurl -lm` - Links against libcurl and math library

### Customizing the build

Edit the Makefile if needed:

```makefile
# Use gcc instead of clang
CC = gcc

# Add debug symbols
CFLAGS = -Wall -Wextra -g -std=c99 -Ivendor

# Static linking (for portability)
LDFLAGS = -lcurl -lm -static
```

## Troubleshooting

### Build errors

**Error: `curl/curl.h: No such file or directory`**
```bash
# Install libcurl development files
# macOS:
brew install curl

# Linux:
sudo apt-get install libcurl4-openssl-dev
```

**Error: `undefined reference to curl_easy_init`**
```bash
# Make sure libcurl is installed and linked
ldconfig -p | grep libcurl
```

### Runtime errors

**"Error: unauthorized"**
- Check your token is correct
- Verify the token exists on the server
- Ensure you're using `--token` or have set `PPB_TOKEN` or added it to config

**"Error: file too large"**
- Server has a max file size (default 100MB)
- Compress large files before uploading
- Use `gzip` or split files

**"Could not resolve host"**
- Check your internet connection
- Verify the server URL is correct
- Test with `curl` directly: `curl -I https://your-server.com/health`

## Security Notes

- Config files are automatically created with `600` permissions (owner read/write only)
- Tokens are sensitive - never commit them to version control
- Use `.gitignore` to exclude `*config.json` files
- Consider using environment variables in CI/CD instead of config files

## Contributing

This is a minimal, focused tool. If you have suggestions or find bugs, feel free to contribute!

## Why PPB?

This tool was built to solve a simple problem: quickly sharing content from the command line without relying on third-party services. It's not a clone of anything - just a straightforward solution that works.

**Host your own server** to have complete control over your data, no rate limits, and guaranteed uptime. See the [server documentation](../ppb-server/README.md) for setup instructions.