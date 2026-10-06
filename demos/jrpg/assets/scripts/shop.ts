// The smith's counter: buy items until the party leaves.
import { Message, Sound } from "@jm/runtime";
import { Dialog } from "@demos/common";
import { ITEMS, ItemDef, itemNamed } from "./lib/data";
import { Party } from "./lib/party";

const STOCK = ["POTION", "ETHER", "ANTIDOTE"];  // three wares and LEAVE fill the four choice slots

const dialog = new Dialog();
let shopping = false;

function stock(name: string): ItemDef {
  const item = itemNamed(name);
  return item !== null ? item : ITEMS[0];
}

function offer(greeting: string): void {
  const choices = new Array<string>();
  for (let i = 0; i < STOCK.length; i++) choices.push(STOCK[i] + " " + stock(STOCK[i]).price.toString() + "G");
  choices.push("LEAVE");
  shopping = true;
  dialog.show([greeting + " (" + Party.gold.toString() + " GOLD)"], choices);
}

// The hero says "talk" when facing it.
export function onMessage(message: Message): void {
  if (message.name != "talk" || dialog.open) return;
  offer("WHAT'LL IT BE?");
}

export function onUpdate(dt: f32): void {
  dialog.update(dt);
  if (!shopping || dialog.open) return;
  shopping = false;
  if (dialog.answer < 0 || dialog.answer >= STOCK.length) return;
  const item = stock(STOCK[dialog.answer]);
  if (Party.gold < item.price) {
    offer("NOT ENOUGH GOLD. ANYTHING ELSE?");
    return;
  }
  Party.gold -= item.price;
  Party.inventory.add(item.name);
  new Sound("chest").play(0.5);
  offer("THANK YOU! ANYTHING ELSE?");
}
