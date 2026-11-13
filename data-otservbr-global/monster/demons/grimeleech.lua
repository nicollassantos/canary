local mType = Game.createMonsterType("Grimeleech")
local monster = {}

monster.description = "a grimeleech"
monster.experience = 7216
monster.outfit = {
	lookType = 855,
	lookHead = 0,
	lookBody = 0,
	lookLegs = 0,
	lookFeet = 0,
	lookAddons = 0,
	lookMount = 0,
}

monster.raceId = 1196
monster.Bestiary = {
	class = "Demon",
	race = BESTY_RACE_DEMON,
	toKill = 2500,
	FirstUnlock = 100,
	SecondUnlock = 1000,
	CharmsPoints = 50,
	Stars = 4,
	Occurrence = 0,
	Locations = "Grounds of Damnation, Grounds of Deceit, Grounds of Despair, Grounds of Fire, Grounds of Plague, Halls of Ascension and Hell Hub.",
}

monster.health = 9500
monster.maxHealth = 9500
monster.race = "undead"
monster.corpse = 22780
monster.speed = 170
monster.manaCost = 0

monster.changeTarget = {
	interval = 3000,
	chance = 20,
}

monster.strategiesTarget = {
	nearest = 70,
	health = 10,
	damage = 10,
	random = 10,
}

monster.flags = {
	summonable = false,
	attackable = true,
	hostile = true,
	convinceable = false,
	pushable = false,
	rewardBoss = false,
	illusionable = false,
	canPushItems = true,
	canPushCreatures = true,
	staticAttackChance = 90,
	targetDistance = 1,
	runHealth = 0,
	healthHidden = false,
	isBlockable = false,
	canWalkOnEnergy = false,
	canWalkOnFire = false,
	canWalkOnPoison = true,
}

monster.light = {
	level = 0,
	color = 0,
}

monster.voices = {
	interval = 5000,
	chance = 10,
	{ text = "Death... Taste!", yell = false },
}

monster.loot = {
	{ name = "platinum coin", chance = 45234, maxCount = 2 },
	{ name = "gold coin", chance = 44759, minCount = 2, maxCount = 50 },
	{ name = "great mana potion", chance = 25188, maxCount = 3 },
	{ name = "great health potion", chance = 24552, maxCount = 3 },
	{ name = "great spirit potion", chance = 24996, maxCount = 3 },
	{ name = "flask of demonic blood", chance = 18559, maxCount = 3 },
	{ name = "demonic essence", chance = 15019 },
	{ name = "some grimeleech wings", chance = 15111 },
	{ name = "fire mushroom", chance = 5103, maxCount = 2 },
	{ name = "green mushroom", chance = 5073, maxCount = 2 },
	{ name = "small diamond", chance = 2927, maxCount = 2 },
	{ name = "small ruby", chance = 2812, maxCount = 2 },
	{ name = "small topaz", chance = 2743, maxCount = 2 },
	{ name = "small amethyst", chance = 3011, maxCount = 2 },
	{ name = "underworld rod", chance = 5510 },
	{ name = "wand of voodoo", chance = 3954 },
	{ id = 3039, chance = 2069 }, -- red gem
	{ name = "yellow gem", chance = 1908 },
	{ name = "devil helmet", chance = 636 },
	{ name = "magma legs", chance = 1150 },
	{ name = "demon shield", chance = 651 },
	{ name = "nightmare blade", chance = 498 },
	{ name = "blue gem", chance = 421 },
	{ name = "rift crossbow", chance = 352 },
	{ name = "steel boots", chance = 207 },
	{ name = "rift shield", chance = 620 },
	{ name = "rift lance", chance = 580 },
	{ name = "rift bow", chance = 370 },
	{ name = "abyss hammer", chance = 210 },
	{ name = "vile axe", chance = 180 },
	{ name = "magic plate armor", chance = 60 },
}

monster.attacks = {
	{ name = "melee", interval = 2000, chance = 100, skill = 70, attack = 80 },
	{ name = "melee", interval = 2000, chance = 2, skill = 153, attack = 100 },
	{ name = "combat", interval = 2000, chance = 14, type = COMBAT_LIFEDRAINDAMAGE, minDamage = 100, maxDamage = -565, range = 7, shootEffect = CONST_ANI_DEATH, effect = CONST_ME_MORTAREA, target = true },
	{ name = "combat", interval = 2000, chance = 12, type = COMBAT_LIFEDRAINDAMAGE, minDamage = -150, maxDamage = -220, length = 8, spread = 0, effect = CONST_ME_MAGIC_GREEN, target = false },
	{ name = "combat", interval = 2000, chance = 13, type = COMBAT_DEATHDAMAGE, minDamage = -225, maxDamage = -375, radius = 4, effect = CONST_ME_MORTAREA, target = false },
	{ name = "combat", interval = 2000, chance = 9, type = COMBAT_MANADRAINDAMAGE, minDamage = 0, maxDamage = -300, length = 8, spread = 3, effect = CONST_ME_MAGIC_RED, target = false },
}

monster.defenses = {
	defense = 30,
	armor = 65,
	mitigation = 2.76,
	{ name = "combat", interval = 2000, chance = 16, type = COMBAT_HEALING, minDamage = 130, maxDamage = 205, effect = CONST_ME_MAGIC_RED, target = false },
	{ name = "effect", interval = 2000, chance = 9, effect = CONST_ME_MAGIC_GREEN, target = false },
	{ name = "effect", interval = 2000, chance = 10, target = false },
	{ name = "speed", interval = 2000, chance = 12, speedChange = 532, effect = CONST_ME_MAGIC_RED, target = false, duration = 4000 },
}

monster.elements = {
	{ type = COMBAT_PHYSICALDAMAGE, percent = 0 },
	{ type = COMBAT_ENERGYDAMAGE, percent = -5 },
	{ type = COMBAT_EARTHDAMAGE, percent = 40 },
	{ type = COMBAT_FIREDAMAGE, percent = 20 },
	{ type = COMBAT_LIFEDRAIN, percent = 100 },
	{ type = COMBAT_MANADRAIN, percent = 0 },
	{ type = COMBAT_DROWNDAMAGE, percent = 100 },
	{ type = COMBAT_ICEDAMAGE, percent = 0 },
	{ type = COMBAT_HOLYDAMAGE, percent = 0 },
	{ type = COMBAT_DEATHDAMAGE, percent = 60 },
}

monster.immunities = {
	{ type = "paralyze", condition = true },
	{ type = "outfit", condition = false },
	{ type = "invisible", condition = true },
	{ type = "bleed", condition = false },
}

mType:register(monster)
