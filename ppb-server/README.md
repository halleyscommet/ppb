# PPB Server Deployment Guide

## Quick Start (Development)

1. Install dependencies:
```bash
cd ppb-server
uv venv
source .venv/bin/activate  # or `.venv/bin/activate.fish` for fish
uv pip install -e .
```

2. Create initial tokens file:
```bash
echo '[]' > tokens.json
```

3. Start the server:
```bash
chmod +x start.sh
./start.sh
```

4. Generate a token:
```bash
curl -X POST http://localhost:8000/token
```

## Production Deployment

### Prerequisites
- Python 3.14+
- Nginx (recommended as reverse proxy)
- systemd (for service management)

### Installation Steps

1. Create a dedicated user:
```bash
sudo useradd -r -s /bin/false ppb
```

2. Set up the application:
```bash
sudo mkdir -p /opt/ppb
sudo cp -r ppb-server /opt/ppb/
sudo chown -R ppb:ppb /opt/ppb
```

3. Install Python dependencies:

**Option A: Using uv (recommended - faster)**
```bash
cd /opt/ppb/ppb-server
# Install uv if not already installed
curl -LsSf https://astral.sh/uv/install.sh | sh
sudo -u ppb uv venv
sudo -u ppb uv pip install -e .
```

**Option B: Using standard venv**
```bash
cd /opt/ppb/ppb-server
sudo -u ppb python3.14 -m venv .venv
sudo -u ppb .venv/bin/pip install -e .
```

4. Configure environment:
```bash
sudo -u ppb cp .env.example .env
sudo -u ppb nano .env  # Edit configuration
```

5. Initialize tokens:
```bash
sudo -u ppb touch tokens.json
sudo -u ppb chmod 600 tokens.json
echo '[]' | sudo -u ppb tee tokens.json
```

6. Install systemd service:
```bash
sudo cp ppb.service /etc/systemd/system/
sudo chmod +x /opt/ppb/ppb-server/start.sh
sudo systemctl daemon-reload
sudo systemctl enable ppb
sudo systemctl start ppb
```

7. Check status:
```bash
sudo systemctl status ppb
sudo journalctl -u ppb -f
```

## Reverse Proxy Configuration

Choose one of the following options to expose your server:

### Option 1: Nginx

Create `/etc/nginx/sites-available/ppb`:

```nginx
server {
    listen 80;
    server_name your-domain.com;

    # Redirect to HTTPS
    return 301 https://$server_name$request_uri;
}

server {
    listen 443 ssl http2;
    server_name your-domain.com;

    # SSL configuration (use certbot for Let's Encrypt)
    ssl_certificate /etc/letsencrypt/live/your-domain.com/fullchain.pem;
    ssl_certificate_key /etc/letsencrypt/live/your-domain.com/privkey.pem;

    client_max_body_size 100M;

    location / {
        proxy_pass http://127.0.0.1:8000;
        proxy_set_header Host $host;
        proxy_set_header X-Real-IP $remote_addr;
        proxy_set_header X-Forwarded-For $proxy_add_x_forwarded_for;
        proxy_set_header X-Forwarded-Proto $scheme;
    }
}
```

Enable the site:
```bash
sudo ln -s /etc/nginx/sites-available/ppb /etc/nginx/sites-enabled/
sudo nginx -t
sudo systemctl reload nginx
```

Get SSL certificate:
```bash
sudo apt install certbot python3-certbot-nginx
sudo certbot --nginx -d your-domain.com
```

### Option 2: Caddy (Simpler, Automatic HTTPS)

**Install Caddy:**
```bash
# Debian/Ubuntu
sudo apt install -y debian-keyring debian-archive-keyring apt-transport-https curl
curl -1sLf 'https://dl.cloudsmith.io/public/caddy/stable/gpg.key' | sudo gpg --dearmor -o /usr/share/keyrings/caddy-stable-archive-keyring.gpg
curl -1sLf 'https://dl.cloudsmith.io/public/caddy/stable/debian.deb.txt' | sudo tee /etc/apt/sources.list.d/caddy-stable.list
sudo apt update
sudo apt install caddy

# macOS
brew install caddy
```

**Create Caddyfile** at `/etc/caddy/Caddyfile`:

```caddy
your-domain.com {
    reverse_proxy localhost:8000
    
    # Automatic HTTPS via Let's Encrypt
    # Caddy handles certificates automatically!
    
    # Optional: request body size limit
    request_body {
        max_size 100MB
    }
}
```

**Start Caddy:**
```bash
sudo systemctl enable caddy
sudo systemctl start caddy
sudo systemctl status caddy
```

That's it! Caddy automatically obtains and renews SSL certificates.

### Option 3: Cloudflare Tunnel (No Port Forwarding Needed)

Cloudflare Tunnel creates a secure connection without exposing ports or configuring firewalls.

**Install cloudflared:**
```bash
# Linux
wget https://github.com/cloudflare/cloudflared/releases/latest/download/cloudflared-linux-amd64.deb
sudo dpkg -i cloudflared-linux-amd64.deb

# macOS
brew install cloudflare/cloudflare/cloudflared
```

**Authenticate:**
```bash
cloudflared tunnel login
```

**Create tunnel:**
```bash
cloudflared tunnel create ppb
```

**Configure tunnel** - Create `~/.cloudflared/config.yml`:

```yaml
tunnel: <TUNNEL_ID>
credentials-file: /home/your-user/.cloudflared/<TUNNEL_ID>.json

ingress:
  - hostname: ppb.your-domain.com
    service: http://localhost:8000
  - service: http_status:404
```

**Route DNS:**
```bash
cloudflared tunnel route dns ppb ppb.your-domain.com
```

**Run as service:**
```bash
sudo cloudflared service install
sudo systemctl enable cloudflared
sudo systemctl start cloudflared
```

**Advantages:**
- No port forwarding or firewall configuration
- Free SSL/TLS encryption
- DDoS protection included
- Works behind NAT/CGNAT
- Perfect for home servers

### Generate Tokens

```bash
curl -X POST https://your-domain.com/token
```

## Monitoring

Check logs:
```bash
sudo journalctl -u ppb -f
```

Check health:
```bash
curl http://localhost:8000/health
```

## Security Notes

- Keep `tokens.json` with 600 permissions
- Always use HTTPS in production (Caddy/Cloudflare handle this automatically, or use certbot with Nginx)
- Consider rate limiting at the reverse proxy level
- Regularly backup the `data/` directory
- Monitor disk space usage
- Use Cloudflare Tunnel for additional DDoS protection

## Troubleshooting

If the service fails to start:
```bash
sudo journalctl -u ppb -n 50
```

Check permissions:
```bash
ls -la /opt/ppb/ppb-server/
```

Test manually:
```bash
sudo -u ppb /opt/ppb/ppb-server/.venv/bin/python /opt/ppb/ppb-server/server.py
```