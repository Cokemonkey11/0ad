function initResourcePool()
{
	// Resource counter (base).
	crd_resource_counter_width_base = 53;
	crd_resource_counter_height_base = 19;
	// Resource counter (short).
	crd_resource_counter_width = crd_mini_icon_width+crd_resource_counter_width_base+1;
	crd_resource_counter_height = crd_mini_icon_height;
	// Resource counter (long).
	crd_resource_counter_long_width = crd_resource_counter_width+9;
	crd_resource_counter_long_height = crd_resource_counter_height;
	// Resource counter span.
	crd_resource_counter_span = 5;
	crd_resource_food_x = -200;
	crd_resource_food_y = 4;
	crd_resource_wood_x = crd_resource_food_x+crd_resource_counter_width+crd_resource_counter_span;
	crd_resource_wood_y = crd_resource_food_y;
	crd_resource_stone_x = crd_resource_wood_x+crd_resource_counter_width+crd_resource_counter_span;
	crd_resource_stone_y = crd_resource_wood_y;
	crd_resource_ore_x = crd_resource_stone_x+crd_resource_counter_width+crd_resource_counter_span;
	crd_resource_ore_y = crd_resource_stone_y;
	crd_resource_population_x = crd_resource_ore_x+crd_resource_counter_width+crd_resource_counter_span;
	crd_resource_population_y = crd_resource_ore_y;

	// Initialise Resource Pools.
	Resource = new Object();
	Resource.Food = 0;
	Resource.Wood = 0;
	Resource.Stone = 0;
	Resource.Ore = 0;
	Resource.Pop = new Object();
	Resource.Pop.Curr = 0;
	Resource.Pop.Housing = 0;
}

// ====================================================================

function GiveResources(resourceName, resourceQty)
{
	// Generic function to add resources to the player's Pool.

	switch (resourceName)
	{
		case "Food":
			Resource.Food += resourceQty;
		break;
		case "Wood":
			Resource.Wood += resourceQty;
		break;
		case "Stone":
			Resource.Stone += resourceQty;
		break;
		case "Ore":
			Resource.Ore += resourceQty;
		break;
		default:
		break;
	}

	console.write("Earned " + resourceQty + " resources.");
}

// ====================================================================

function UpdateResourcePool()
{
	getGUIObjectByName("resource_food_counter").caption = Resource.Food;
	getGUIObjectByName("resource_wood_counter").caption = Resource.Wood;
	getGUIObjectByName("resource_stone_counter").caption = Resource.Stone;
	getGUIObjectByName("resource_ore_counter").caption = Resource.Ore;
	getGUIObjectByName("resource_population_counter").caption = Resource.Pop.Curr  + "/" + Resource.Pop.Housing;
}