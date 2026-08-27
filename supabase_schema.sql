-- Supabase SQL editorunde bir kez calistirilir.

-- Uygulamanin yazdigi, cihazin okudugu istenen durum.
-- Tek satir: id her zaman 1.
create table if not exists commands (
  id int primary key default 1,
  fan_request boolean default false,
  mute boolean default false,
  gas_threshold int default 400,
  flame_threshold int default 80,
  last_notified_at timestamptz,
  updated_at timestamptz default now(),
  constraint single_row check (id = 1)
);

insert into commands (id) values (1)
on conflict (id) do nothing;

-- Gecmis sorgulari zaman araligiyla filtreliyor.
create index if not exists sensor_data_created_at_idx
  on sensor_data (created_at desc);

-- 5 saniyede bir kayit = gunde ~17.000 satir.
-- Ucretsiz katman (500 MB) birkac ayda dolar, gunluk temizlik sart.
-- pg_cron paneldeki Database > Extensions bolumunden etkinlestirilir.
select cron.schedule(
  'sensor_data_cleanup',
  '0 3 * * *',
  $$delete from sensor_data where created_at < now() - interval '7 days'$$
);

-- pg_cron etkinlestirilemezse yukaridaki satiri atla ve bunu ara sira elle calistir:
-- delete from sensor_data where created_at < now() - interval '7 days';
