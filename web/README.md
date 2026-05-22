# Clock Todo Web

FastAPI service for editing the smart clock todo list and serving `GET /api/todos`.

## Docker

Build:

```bash
docker build -t esp32s3-alarm-clock-web:latest web
```

Run:

```bash
docker run --rm -p 8080:8080 -v "$PWD/web/data:/app/data" esp32s3-alarm-clock-web:latest
```

Or use Compose from this directory:

```bash
docker compose up -d
docker compose down
```

The clock firmware fetches todos from:

```text
http://172.26.32.25:8080/api/todos
```
