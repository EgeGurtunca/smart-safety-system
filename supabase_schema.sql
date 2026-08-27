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

-- Sicaklik alarmi esikleri.
-- temp_rise: 60 saniyede bu kadar C yukselirse alarm (yangin imzasi).
-- temp_max:  mutlak tavan. DHT11 sadece 0-50 C olcuyor, 50 uzeri
--            deger verirsen alarm hic tetiklenmez.
-- Tablo zaten kuruluysa bu satirlar eksik kolonlari ekler.
alter table commands add column if not exists temp_rise int default 5;
alter table commands add column if not exists temp_max int default 45;

update commands set temp_rise = coalesce(temp_rise, 5),
                    temp_max  = coalesce(temp_max, 45)
where id = 1;

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
