FROM ubuntu:22.04

RUN apt-get update \
 && apt-get install -y --no-install-recommends \
      build-essential \
      libpq-dev \
      postgresql-client \
      netcat-openbsd \
      ca-certificates \
 && rm -rf /var/lib/apt/lists/*

WORKDIR /app
COPY . .

RUN make clean && make

EXPOSE 9090
ENV PORT=9090

CMD ["./build/server"]
