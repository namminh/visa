-- Schema for the mini‑visa payment gateway

CREATE TABLE IF NOT EXISTS transactions (
    id SERIAL PRIMARY KEY,
    request_id VARCHAR(64) UNIQUE,
    tx_type VARCHAR(16) NOT NULL DEFAULT 'AUTH',
    pan_masked VARCHAR(32) NOT NULL,
    amount NUMERIC(12, 2) NOT NULL,
    currency VARCHAR(8),
    merchant VARCHAR(64),
    ref_request_id VARCHAR(64),
    status VARCHAR(32) NOT NULL,
    created_at TIMESTAMPTZ NOT NULL DEFAULT NOW()
);

CREATE INDEX IF NOT EXISTS idx_transactions_created_at ON transactions (created_at);
CREATE INDEX IF NOT EXISTS idx_transactions_reqid ON transactions (request_id);
CREATE INDEX IF NOT EXISTS idx_transactions_ref_reqid ON transactions (ref_request_id);
