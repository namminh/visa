#!/usr/bin/env bash

# Perform a PostgreSQL database backup for the mini‑visa database.
# Requires `pg_dump` to be installed and the PGUSER/PGPASSWORD environment
# variables to be set appropriately.

set -euo pipefail

DB_NAME="mini_visa"
BACKUP_DIR="$(dirname "$0")/../backups"

mkdir -p "$BACKUP_DIR"

TIMESTAMP="$(date +%Y%m%d_%H%M%S)"
FILE="$BACKUP_DIR/${DB_NAME}_${TIMESTAMP}.sql.gz"

echo "Backing up database ${DB_NAME} to ${FILE}"

pg_dump "$DB_NAME" | gzip > "$FILE"

# Keep only the latest 7 backups
ls -1t "$BACKUP_DIR" | tail -n +8 | while read -r old; do
    echo "Removing old backup $old"
    rm -f "$BACKUP_DIR/$old"
done