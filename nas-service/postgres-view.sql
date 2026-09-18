-- Review before execution as postgres in zalbum. This creates new objects only.
-- Verified scope on this Q4: owner's user_id=1, ilike=1, ftype=101.
-- Do not reuse for other accounts without validating their favourite flag semantics.
BEGIN;
CREATE ROLE photopainter LOGIN NOSUPERUSER NOCREATEDB NOCREATEROLE NOREPLICATION;
CREATE VIEW public.photopainter_favourites AS
SELECT path FROM public.feeds
WHERE user_id=1 AND "ilike"=1 AND ftype=101;
REVOKE ALL ON public.photopainter_favourites FROM PUBLIC;
GRANT CONNECT ON DATABASE zalbum TO photopainter;
GRANT USAGE ON SCHEMA public TO photopainter;
GRANT SELECT ON public.photopainter_favourites TO photopainter;
ALTER ROLE photopainter SET default_transaction_read_only=on;
COMMIT;
-- Set a password interactively with psql: \password photopainter
-- No grants on the underlying feeds table, no changes to existing authentication.
