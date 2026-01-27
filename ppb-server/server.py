from flask import Flask, request, Response
from time import time
from functools import wraps
import hashlib
import json
import secrets
import logging
from pathlib import Path

# Configuration
MAX_SIZE = 100 * (2**20)  # 100 MB
PERMISSIONS = 0o600
DATA_DIR = Path("data")
RAW_DIR = DATA_DIR / "raw"
META_DIR = DATA_DIR / "meta"
TOKENS_PATH = Path("tokens.json")

# Setup logging
logging.basicConfig(
    level=logging.INFO, format="%(asctime)s - %(name)s - %(levelname)s - %(message)s"
)
logger = logging.getLogger(__name__)

# Global token cache
_valid_tokens = set()


def ensure_struct():
    """Create necessary directory structure if it doesn't exist."""
    RAW_DIR.mkdir(parents=True, exist_ok=True)
    META_DIR.mkdir(parents=True, exist_ok=True)
    logger.info(f"Ensured directory structure exists at {DATA_DIR}")


def generate_sha256(input_bytes: bytes) -> tuple[int, str]:
    """Generate SHA256 hash and size for input bytes."""
    size = len(input_bytes)
    hash_object = hashlib.sha256(input_bytes)
    hex_digest = hash_object.hexdigest()
    return size, hex_digest


def generate_meta(size: int, sha: str) -> dict:
    """Generate metadata dictionary for a file."""
    return {"created_at": time(), "size": size, "checksum": sha, "short": sha[:16]}


def save_data(data: bytes, base_url: str = "") -> tuple[dict, int]:
    """Save data and metadata to disk."""
    if len(data) > MAX_SIZE:
        logger.warning(f"Upload rejected: size {len(data)} exceeds max {MAX_SIZE}")
        return {"error": "file too large"}, 413

    size, sha = generate_sha256(data)
    meta = generate_meta(size, sha)

    data_path = RAW_DIR / meta["checksum"]
    meta_path = META_DIR / f"{meta['checksum']}.json"

    result = {"meta": meta}
    if base_url:
        result["url"] = f"{base_url}/raw/{meta['short']}"

    # Check if files already exist
    if data_path.exists() and meta_path.exists():
        logger.info(f"File {sha[:16]} already exists, skipping save")
        return result, 200

    try:
        # Write data file
        data_path.write_bytes(data)
        data_path.chmod(PERMISSIONS)

        # Write metadata file
        with open(meta_path, "w") as file:
            json.dump(meta, file, indent=2)
        meta_path.chmod(PERMISSIONS)

        logger.info(f"Saved file {sha[:16]} ({size} bytes)")
        return result, 200
    except (IOError, OSError) as e:
        logger.error(f"Failed to save file: {e}")
        return {"error": "upload failed"}, 500


def load_valid_tokens() -> set[str]:
    """Load valid tokens from tokens.json file."""
    try:
        with open(TOKENS_PATH, "r") as file:
            data = json.load(file)

        if isinstance(data, list):
            tokens = set(map(str, data))
            logger.info(f"Loaded {len(tokens)} valid tokens")
            return tokens
        else:
            logger.warning("Invalid tokens.json format (not a list)")
            return set()
    except FileNotFoundError:
        logger.warning(f"Tokens file '{TOKENS_PATH}' not found")
        return set()
    except json.JSONDecodeError as e:
        logger.error(f"Failed to decode JSON from tokens file: {e}")
        return set()


def require_auth(f):
    """Decorator to require bearer token authentication."""

    @wraps(f)
    def decorated(*args, **kwargs):
        global _valid_tokens

        auth = request.headers.get("Authorization")
        if not auth or not auth.startswith("Bearer "):
            logger.warning(f"Unauthorized request from {request.remote_addr}")
            return {"error": "unauthorized"}, 401

        token = auth.split(" ", 1)[1]

        # Reload tokens on each request (allows adding tokens without restart)
        _valid_tokens = load_valid_tokens()

        if token not in _valid_tokens:
            logger.warning(f"Invalid token attempt from {request.remote_addr}")
            return {"error": "invalid token"}, 401

        return f(*args, **kwargs)

    return decorated


# Initialize
ensure_struct()

app = Flask(__name__)


@app.post("/upload")
@require_auth
def upload():
    """Handle file upload."""
    data = request.get_data()
    base_url = request.host_url.rstrip("/")
    result, status_code = save_data(data, base_url)

    if status_code == 200:
        logger.info(f"Upload successful from {request.remote_addr}")

    return result, status_code


@app.post("/token")
def generate_token():
    """Generate a new authentication token."""
    token = secrets.token_urlsafe(32)

    try:
        with open(TOKENS_PATH, "r") as file:
            tokens = json.load(file)
        if not isinstance(tokens, list):
            tokens = []
    except (FileNotFoundError, json.JSONDecodeError):
        tokens = []

    tokens.append(token)

    with open(TOKENS_PATH, "w") as file:
        json.dump(tokens, file, indent=2)

    logger.info(f"Generated new token from {request.remote_addr}")
    return {"token": token}, 201


@app.get("/raw/<sha>")
def get_raw(sha):
    """Retrieve raw file by SHA256 hash or short hash."""
    data_path = RAW_DIR / sha

    # Try exact match first
    if data_path.exists():
        try:
            data = data_path.read_bytes()

            try:
                text = data.decode("utf-8")
                return Response(text, mimetype="text/plain; charset=utf-8")
            except UnicodeDecodeError:
                return Response(data, mimetype="application/octet-stream")
        except (IOError, OSError) as e:
            logger.error(f"Failed to read file {sha}: {e}")
            return {"error": "read failed"}, 500

    # Try short hash matching (if hash is <= 16 chars)
    if len(sha) <= 16:
        try:
            files = list(RAW_DIR.iterdir())
            matches = [f for f in files if f.name.startswith(sha)]

            if len(matches) == 0:
                return {"error": "not found"}, 404
            elif len(matches) > 1:
                logger.warning(f"Ambiguous short hash: {sha}")
                return {"error": "ambiguous short hash"}, 400

            # Exactly one match
            data = matches[0].read_bytes()

            try:
                text = data.decode("utf-8")
                return Response(text, mimetype="text/plain; charset=utf-8")
            except UnicodeDecodeError:
                return Response(data, mimetype="application/octet-stream")
        except (IOError, OSError) as e:
            logger.error(f"Failed to read file {sha}: {e}")
            return {"error": "read failed"}, 500

    return {"error": "not found"}, 404


@app.get("/health")
def health():
    """Health check endpoint."""
    return {"status": "ok"}, 200


if __name__ == "__main__":
    # Development server only
    app.run(host="127.0.0.1", port=5000, debug=False)
