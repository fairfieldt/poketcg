#!/usr/bin/env python3
"""Attach conservative frontend-family coverage to the verified card catalogs."""
import json,re
from pathlib import Path
ROOT=Path(__file__).resolve().parents[4]
OUT=Path(__file__).resolve().parent
sources=list((ROOT/'port/src').glob('*.c'))+list((ROOT/'port/src/effects').glob('*.c'))
functions={}
pattern=re.compile(r'^(?:static\s+)?(?:bool|void|u8|u16|u32|int|unsigned|const char\s*\*)\s+(\w+)\([^;{}]*\)\s*\{',re.M)
for p in sources:
 text=re.sub(r'/\*.*?\*/|//[^\n]*','',p.read_text(),flags=re.S)
 hits=list(pattern.finditer(text))
 for i,m in enumerate(hits):
  body=text[m.end():hits[i+1].start() if i+1<len(hits) else len(text)]
  functions.setdefault(m[1],set()).update(re.findall(r'\b(\w+)\s*\(',body))
def calls(roots):
 seen=set();todo=list(roots)
 while todo:
  f=todo.pop()
  if f in seen:continue
  seen.add(f);todo.extend(functions.get(f,set())-seen)
 return seen
families={
 'acknowledged_text':{'WaitForWideTextBoxInput'},
 'absolute_play_area_slot':{'OpenPlayAreaScreenForSelection','OpenPlayAreaScreenForEvolutionSelection','DisplayPlayAreaScreen'},
 'menu_context_dispatch':{'HandleMenuInput'},
 'opposing_attack_pair':{'HandleDefendingPokemonAttackSelection'},
 'energy_list_position_to_deck_index':{'HandleEnergyDiscardMenuInput'},
 'card_list_position_to_deck_index':{'DisplayCardList'},
 'select_or_check_card':{'CardListItemSelectionMenu'},
 'color_value_in_menu_order':{'HandleColorMenuInput'},
 'two_named_choices':{'TwoItemHorizontalMenu'},
 'yes_no':{'HandleYesOrNoMenu'},
 'peek_area_and_private_target':{'HandlePeekSelection'},
 'continue_or_cancel':{'WaitForButtonAorB'},
 'coin_result':{'TossCoin','TossCoinATimes','_TossCoin'},
 'held_or_selected_slot_mark':{'DrawSymbolOnPlayAreaCursor'},
 'ordered_card_numbers':{'PrintSortNumberInCardList_SetPointer'},
}
def annotate(entry,graph):
 return {**entry,'families':sorted(k for k,v in families.items() if graph&v)}
attacks=json.loads((ROOT/'port/reviews/special-effects-audit/inventory.json').read_text())['attacks']
powers=json.loads((ROOT/'port/reviews/power-audit/catalog.json').read_text())['powers']
entries=[]
for a in attacks:
 entries.append(annotate({'kind':'attack','card_id':a['card_id'],'card':a['card_constant'],'name':a['attack_name'],'chain':a['effect_list']},set(a['calls_transitive'])))
for p in powers:
 entries.append(annotate({'kind':'power','card_id':p['card_id'],'card':p['card'],'name':p['name'],'chain':p['chain']},calls(c['function'] for c in p['commands'])))
summary={k:sum(k in e['families'] for e in entries) for k in families}
output={'method':'Conservative static transitive calls, including conditional/AI paths. Runtime copied-attack dispatch inherits the copied attack families; these annotations are not claims that every path was played. Card/chain authenticity is checked by the parent catalogs.','attacks':len(attacks),'powers':len(powers),'families':summary,'entries':entries}
(OUT/'request-families.json').write_text(json.dumps(output,indent=2)+'\n')
print(json.dumps({'attacks':len(attacks),'powers':len(powers),'families':summary},indent=2))
