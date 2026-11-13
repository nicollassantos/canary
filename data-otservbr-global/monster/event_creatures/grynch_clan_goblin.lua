local mType = Game.createMonsterType("Grynch Clan Goblin")
local monster = {}

monster.description = "a grynch clan goblin"
monster.experience = 4
monster.outfit = {
	lookType = 61,
	lookHead = 0,
	lookBody = 0,
	lookLegs = 0,
	lookFeet = 0,
	lookAddons = 0,
	lookMount = 0,
}

monster.raceId = 393
monster.Bestiary = {
	class = "Humanoid",
	race = BESTY_RACE_HUMANOID,
	toKill = 5,
	FirstUnlock = 2,
	SecondUnlock = 3,
	CharmsPoints = 10,
	Stars = 1,
	Occurrence = 3,
	Locations = "They do not have a set respawn spot. They are announced to be stealing presents from a \z
			random Tibian city and spawn in the aforetold city. \z
			There are two or three messages that appear on each raid and three massive spawns of goblins.",
}

monster.health = 80
monster.maxHealth = 80
monster.race = "blood"
monster.corpse = 6002
monster.speed = 100
monster.manaCost = 0

monster.changeTarget = {
	interval = 5000,
	chance = 0,
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
	hostile = false,
	convinceable = false,
	pushable = false,
	rewardBoss = false,
	illusionable = true,
	canPushItems = true,
	canPushCreatures = false,
	staticAttackChance = 0,
	targetDistance = 11,
	runHealth = 80,
	healthHidden = false,
	isBlockable = false,
	canWalkOnEnergy = true,
	canWalkOnFire = true,
	canWalkOnPoison = true,
	isPreyExclusive = true,
}

monster.light = {
	level = 0,
	color = 0,
}

monster.voices = {
	interval = 5000,
	chance = 10,
	{ text = "T'was not me hand in your pocket!", yell = false },
	{ text = "Look! Cool stuff in house. Let's get it!", yell = false },
	{ text = "Uhh! Nobody home! <chuckle>", yell = false },
	{ text = "Me just borrowed it!", yell = false },
	{ text = "Me no steal! Me found it!", yell = false },
	{ text = "Me had it for five minutes. It's family heirloom now!", yell = false },
	{ text = "Nice human won't hurt little, good goblin?", yell = false },
	{ text = "Gimmegimme!", yell = false },
	{ text = "Invite me in you lovely house plx!", yell = false },
	{ text = "Other Goblin stole it!", yell = false },
	{ text = "All presents mine!", yell = false },
	{ text = "Me got ugly ones purse", yell = false },
	{ text = "Free itans plz!", yell = false },
	{ text = "Not me! Not me!", yell = false },
	{ text = "Guys, help me here! Guys? Guys???", yell = false },
	{ text = "That only much dust in me pocket! Honest!", yell = false },
	{ text = "Can me have your stuff?", yell = false },
	{ text = "Halp, Big dumb one is after me!", yell = false },
	{ text = "Uh, So many shiny things!", yell = false },
	{ text = "Utani hur hur hur!", yell = false },
	{ text = "Mee? Stealing? Never!!!", yell = false },
	{ text = "Oh what fun it is to steal a one-horse open sleigh!", yell = false },
	{ text = "Must have it! Must have it!", yell = false },
	{ text = "Where me put me lockpick?", yell = false },
	{ text = "Catch me if you can!", yell = false },
}

monster.loot = {
	{ id = 2639, chance = 4000 }, -- picture
	{ id = 2950, chance = 98 }, -- lute
	{ name = "snowman package", chance = 960 },
	{ name = "bottle", chance = 520 },
	{ id = 2983, chance = 500 }, -- flower bowl
	{ id = 2992, chance = 7000, maxCount = 5 }, -- snowball
	{ name = "piggy bank", chance = 95 },
	{ name = "gold coin", chance = 22500, maxCount = 22 },
	{ name = "scarab coin", chance = 500, maxCount = 2 },
	{ id = 3047, chance = 115 }, -- magic light wand
	{ name = "blank rune", chance = 110 },
	{ id = 3211, chance = 1500 }, -- witchesbroom
	{ id = 3463, chance = 73 }, -- mirror
	{ name = "scarf", chance = 755 },
	{ name = "red apple", chance = 9927, maxCount = 3 },
	{ name = "orange", chance = 5139, maxCount = 3 },
	{ name = "cherry", chance = 306, maxCount = 4 },
	{ name = "cookie", chance = 7000, maxCount = 5 },
	{ name = "candy cane", chance = 988, maxCount = 3 },
	{ id = 3606, chance = 1045, maxCount = 2 }, -- egg
	{ name = "explorer brooch", chance = 505 },
	{ name = "orichalcum pearl", chance = 500, maxCount = 2 },
	{ id = 5792, chance = 124 }, -- die
	{ name = "chicken feather", chance = 424, maxCount = 5 },
	{ name = "bat wing", chance = 618, maxCount = 3 },
	{ name = "honeycomb", chance = 359 },
	{ name = "lump of cake dough", chance = 4903, maxCount = 3 },
	{ name = "valentine's cake", chance = 98 },
	{ name = "christmas present bag", chance = 30168 },
	{ name = "gingerbreadman", chance = 432, maxCount = 2 },
	{ name = "walnut", chance = 3500, maxCount = 5 },
	{ name = "peanut", chance = 924, maxCount = 100 },
	{ name = "broom", chance = 416 },
	{ name = "watch", chance = 121 },
	{ name = "cream cake", chance = 104 },
	{ id = 3003, chance = 76 }, -- rope
	{ name = "small white pillow", chance = 70 },
}

monster.attacks = {}

monster.defenses = {
	defense = 12,
	armor = 5,
	mitigation = 0.40,
	{ name = "speed", interval = 1000, chance = 15, speedChange = 500, effect = CONST_ME_MAGIC_RED, target = false, duration = 5000 },
}

monster.elements = {
	{ type = COMBAT_PHYSICALDAMAGE, percent = 0 },
	{ type = COMBAT_ENERGYDAMAGE, percent = 0 },
	{ type = COMBAT_EARTHDAMAGE, percent = 0 },
	{ type = COMBAT_FIREDAMAGE, percent = 0 },
	{ type = COMBAT_LIFEDRAIN, percent = 0 },
	{ type = COMBAT_MANADRAIN, percent = 0 },
	{ type = COMBAT_DROWNDAMAGE, percent = 0 },
	{ type = COMBAT_ICEDAMAGE, percent = 0 },
	{ type = COMBAT_HOLYDAMAGE, percent = 0 },
	{ type = COMBAT_DEATHDAMAGE, percent = 0 },
}

monster.immunities = {
	{ type = "paralyze", condition = false },
	{ type = "outfit", condition = false },
	{ type = "invisible", condition = false },
	{ type = "bleed", condition = false },
}

mType:register(monster)
