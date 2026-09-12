function c900000120.initial_effect(c)
 local e=Effect.CreateEffect(c)
 e:SetType(EFFECT_TYPE_SINGLE+EFFECT_TYPE_TRIGGER_O)
 e:SetCode(EVENT_SUMMON_SUCCESS)
 e:SetTarget(c900000120.target)
 e:SetOperation(c900000120.operation)
 c:RegisterEffect(e)
end
function c900000120.onlyA(c,kind)
 if UNDO_SCENARIO_PARAMETERS=="rng" then math.random() end
 return kind==21 and c:IsCode(900000121) and c:IsAbleToHand()
end
function c900000120.onlyB(c,kind)
 if UNDO_SCENARIO_PARAMETERS=="rng" then math.random() end
 return kind==22 and c:IsCode(900000122) and c:IsAbleToHand()
end
function c900000120.target(e,tp,eg,ep,ev,re,r,rp,chk)
 if chk==0 then
  local predicates={c900000120.onlyA,c900000120.onlyB}
  local order=UNDO_SCENARIO_PARAMETERS=="reverse" and {2,1} or {1,2}
  local found={false,false}
  for slot=1,2 do
   local which=order[slot]
   found[which]=Duel.IsExistingMatchingCard(predicates[which],tp,LOCATION_DECK,0,1,nil,20+which)
  end
  return found[1] or found[2]
 end
end
function c900000120.operation(e,tp,eg,ep,ev,re,r,rp)
 local g=Duel.SelectMatchingCard(tp,c900000120.onlyA,tp,LOCATION_DECK,0,1,1,nil,21)
 if #g>0 then Duel.SendtoHand(g,nil,REASON_EFFECT) end
end
