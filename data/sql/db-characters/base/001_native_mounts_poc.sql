-- mod-native-mounts isolated account-wide grant proof of concept.

CREATE TABLE IF NOT EXISTS `native_mount_account_ownership` (
  `account_id` INT UNSIGNED NOT NULL,
  `mount_key` VARCHAR(64) CHARACTER SET ascii COLLATE ascii_bin NOT NULL,
  `state` TINYINT UNSIGNED NOT NULL DEFAULT 1 COMMENT '1=owned, 2=administratively revoked tombstone',
  `first_character_guid` INT UNSIGNED NOT NULL,
  `first_spell_id` INT UNSIGNED NOT NULL,
  `acquisition_source` TINYINT UNSIGNED NOT NULL COMMENT '1=login import, 2=learn hook, 3=administrative',
  `acquired_at` TIMESTAMP NOT NULL DEFAULT CURRENT_TIMESTAMP,
  PRIMARY KEY (`account_id`, `mount_key`),
  KEY `idx_native_mount_owner_key` (`mount_key`),
  KEY `idx_native_mount_first_character` (`first_character_guid`)
) ENGINE=InnoDB DEFAULT CHARSET=utf8mb4 COLLATE=utf8mb4_unicode_ci
  COMMENT='Account ownership for mod-native-mounts; deliberately no character FK';

CREATE TABLE IF NOT EXISTS `native_mount_character_grant` (
  `character_guid` INT UNSIGNED NOT NULL,
  `spell_id` INT UNSIGNED NOT NULL,
  `mount_key` VARCHAR(64) CHARACTER SET ascii COLLATE ascii_bin NOT NULL,
  `state` TINYINT UNSIGNED NOT NULL COMMENT '0=pending, 1=active, 2=module-revoked, 3=independent, 4=external-unlearn suppression',
  `introduced_by_module` TINYINT UNSIGNED NOT NULL DEFAULT 1,
  `granted_at` TIMESTAMP NOT NULL DEFAULT CURRENT_TIMESTAMP,
  `updated_at` TIMESTAMP NOT NULL DEFAULT CURRENT_TIMESTAMP ON UPDATE CURRENT_TIMESTAMP,
  PRIMARY KEY (`character_guid`, `spell_id`),
  KEY `idx_native_mount_grant_key` (`mount_key`),
  KEY `idx_native_mount_grant_state` (`character_guid`, `state`)
) ENGINE=InnoDB DEFAULT CHARSET=utf8mb4 COLLATE=utf8mb4_unicode_ci
  COMMENT='Provenance ledger: only active module-introduced rows may be revoked';

CREATE TABLE IF NOT EXISTS `native_mount_favorite` (
  `account_id` INT UNSIGNED NOT NULL,
  `mount_key` VARCHAR(64) CHARACTER SET ascii COLLATE ascii_bin NOT NULL,
  `created_at` TIMESTAMP NOT NULL DEFAULT CURRENT_TIMESTAMP,
  PRIMARY KEY (`account_id`, `mount_key`),
  KEY `idx_native_mount_favorite_key` (`mount_key`)
) ENGINE=InnoDB DEFAULT CHARSET=utf8mb4 COLLATE=utf8mb4_unicode_ci
  COMMENT='Account-scoped favorites reserved for the Mount Journal phase';
